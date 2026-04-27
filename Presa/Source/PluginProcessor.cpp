#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "capture/LoopbackCapture.h"


// File-based debug log — works even from Spotlight launches where stderr is lost
static void presaLog (const char* msg)
{
    if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
    {
        fprintf (f, "%s\n", msg);
        fclose (f);
    }
}
static void presaLog (const juce::String& msg) { presaLog (msg.toRawUTF8()); }

PresaProcessor::PresaProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    // Input bus is declared but disabled by default (`false`).
    // Why: the JUCE standalone host only opens an audio device — and therefore
    // only drives processBlock — if the AudioProcessor has at least one bus
    // that can be tied to the device's I/O. Capture is fed by ScreenCaptureKit
    // directly into CaptureBuffer, so we don't need a live input device; the
    // disabled input bus exists purely to keep the audio thread running.
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), false)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    // Distribute pads across the state-dot field with organic jitter
    static const float kJitter[16][2] = {
        { 0.10f, 0.08f }, { 0.38f, 0.15f }, { 0.62f, 0.05f }, { 0.88f, 0.18f },
        { 0.15f, 0.32f }, { 0.42f, 0.40f }, { 0.70f, 0.28f }, { 0.92f, 0.35f },
        { 0.08f, 0.58f }, { 0.35f, 0.68f }, { 0.55f, 0.62f }, { 0.85f, 0.72f },
        { 0.18f, 0.85f }, { 0.45f, 0.90f }, { 0.68f, 0.82f }, { 0.90f, 0.95f },
    };
    for (int i = 0; i < kNumPads; ++i)
    {
        pads[i].dotX = kJitter[i][0];
        pads[i].dotY = kJitter[i][1];

        // Root note defaults to the pad's own trigger note so a native pad
        // press (midi = kMidiBase + i) plays back at the sample's original
        // speed. With the old default of C4 (60), pad 0 played at 2^(-2) =
        // 0.25x speed (two octaves below root), which sounds like a severely
        // stretched sample rather than a pitched-down one. Any re-pitching
        // relative to this root is explicit via incoming MIDI notes.
        pads[i].rootNote = kMidiBase + i;
    }

}

PresaProcessor::~PresaProcessor()
{
    if (loopbackCapture)
    {
        loopbackCapture->stopCapture();
        loopbackCapture.reset();
    }
}

void PresaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    presaLog ("prepareToPlay: hostSampleRate=" + juce::String (sampleRate, 2)
              + " samplesPerBlock=" + juce::String (samplesPerBlock));

    currentSampleRate.store (sampleRate);
    currentBlockSize.store (samplesPerBlock);
    captureBuffer.prepare (sampleRate, 2);
    samplePlayer.prepare (sampleRate, samplesPerBlock);
    fxChain.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    ambientEngine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // Create loopback capture if not yet created
    if (!loopbackCapture)
    {
        loopbackCapture = LoopbackCapture::create (captureBuffer);
        loopbackDeviceStatus = loopbackCapture->getDeviceStatus();
    }
}

void PresaProcessor::releaseResources()
{
    prepareToStop();
}

void PresaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOut = getTotalNumOutputChannels();

    // Tripwire — fprintf(stderr) survives Release, is independent of JUCE's
    // DBG macro, and is independent of the /tmp log file. If you see this in
    // the terminal, the audio thread is definitely alive.
    {
        static std::atomic<int> heartbeat { 0 };
        int n = heartbeat.fetch_add (1);
        if (n == 0 || (n % 500) == 0)
        {
            fprintf (stderr, "processBlock running: #%d numSamples=%d numOut=%d\n",
                     n, numSamples, numOut);
            fflush (stderr);
            presaLog ("processBlock heartbeat #" + juce::String (n)
                      + " numSamples=" + juce::String (numSamples)
                      + " numOut=" + juce::String (numOut));
        }
    }

    // ── 1. Clear output buffer (LoopbackCapture feeds CaptureBuffer directly) ──
    for (int ch = 0; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Check if capture just finished — commit slice and load into player
    if (captureBuffer.hasNewSlice())
    {
        auto slice = captureBuffer.commitSlice();

        float sliceRms = 0.0f;
        if (slice.getNumSamples() > 0 && slice.getNumChannels() > 0)
            sliceRms = slice.getRMSLevel (0, 0, slice.getNumSamples());

        presaLog ("processBlock: commitSlice returned "
             + juce::String (slice.getNumSamples()) + " samples, "
             + juce::String (slice.getNumChannels()) + " channels, RMS="
             + juce::String (sliceRms, 6));

        if (slice.getNumSamples() > 0)
        {
            // The slice was written by the ScreenCaptureKit audio thread, not
            // the host audio callback. Its sample rate is whatever SCK is
            // delivering (normally 48 kHz, but we read the actual value from
            // the ASBD in the first SCK callback). Using currentSampleRate
            // here would assume the capture rate equals the host output rate,
            // which silently detunes playback when they differ.
            int sr = 48000;
            if (loopbackCapture != nullptr)
                sr = static_cast<int> (loopbackCapture->getCaptureSampleRate());
            samplePlayer.setSample (std::move (slice), sr);
            presaLog ("processBlock: setSample done, samplePlayer.sampleLength="
                 + juce::String (samplePlayer.getSampleBuffer().getNumSamples())
                 + " srcSR=" + juce::String (sr)
                 + " hostSR=" + juce::String (currentSampleRate.load(), 2));

            // Fresh capture resets the trim window to the full sample.
            trimStart.store (0.0f);
            trimEnd.store   (1.0f);

            // Update pad slice regions (normalised 0..1)
            for (int i = 0; i < kNumPads; ++i)
            {
                auto region = samplePlayer.getSlice (i);
                int totalLen = samplePlayer.getSampleBuffer().getNumSamples();
                if (totalLen > 0)
                {
                    pads[i].sliceStart = static_cast<float> (region.start) / totalLen;
                    pads[i].sliceEnd   = static_cast<float> (region.end)   / totalLen;
                }
            }

            newSampleReady.store (true);
        }
    }

    // Process MIDI. Routing depends on the sampler mode: SLICE maps
    // notes 36–51 (C2–D#3) onto pads 0–15 so each pad plays its chunk;
    // SAMPLE routes every note through triggerSamplePlayback so the full
    // sample retunes relative to C4 (the SAMPLE-mode root).
    const bool sampleModeRouting = (samplerMode.load() == SamplerMode::Sample);
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            if (sampleModeRouting)
            {
                triggerSamplePlayback (note);
            }
            else
            {
                int padIndex = note - kMidiBase;
                if (padIndex >= 0 && padIndex < kNumPads)
                    samplePlayer.noteOn (padIndex, note, pads[padIndex].rootNote);
            }
        }
        else if (msg.isNoteOff())
        {
            int note = msg.getNoteNumber();
            if (sampleModeRouting)
            {
                stopSamplePlayback();
            }
            else
            {
                int padIndex = note - kMidiBase;
                if (padIndex >= 0 && padIndex < kNumPads)
                    samplePlayer.noteOff (padIndex);
            }
        }
    }

    // Render sample player output
    samplePlayer.processBlock (buffer, buffer.getNumSamples());

    // ── Ambient feedback (SAMPLE mode only) ───────────────────────────────
    // Sits between the sample renderer and the global FX chain so evolving
    // texture feeds through saturation/degrade/reverb like any other source.
    if (samplerMode.load() == SamplerMode::Sample)
    {
        const int selAE = juce::jlimit (0, kNumPads - 1, selectedPad.load());
        ambientEngine.processBlock (buffer,
                                    pads[selAE].dotX,
                                    pads[selAE].dotY);
    }

    // Global FX chain: driven by the currently-selected pad's StateDot.
    // dotX = LIGHT(0) → WORN(1); dotY = FIXED(0) → BREATHING(1).
    // Cheap — just updates smoothed targets once per block, not per-sample.
    const int sel = juce::jlimit (0, kNumPads - 1, selectedPad.load());
    fxChain.setWornAxis      (pads[sel].dotX);
    fxChain.setBreathingAxis (pads[sel].dotY);
    fxChain.processBlock (buffer);
}

// ── Capture control ────────────────────────────────────────────────────────

void PresaProcessor::armCapture()
{
    presaLog ("armCapture() called");

    captureBuffer.arm();
    captureState.store (static_cast<int> (CaptureState::Armed));

    // Start ScreenCaptureKit-based system audio capture. On first call this
    // triggers the Screen Recording permission prompt; subsequent calls reuse
    // the granted permission with no UI.
    if (loopbackCapture && !loopbackCapture->isCapturing())
    {
        DBG ("PresaProcessor: starting ScreenCaptureKit capture");
        auto error = loopbackCapture->startCapture();
        loopbackDeviceStatus = loopbackCapture->getDeviceStatus();

        if (error.isNotEmpty())
        {
            captureErrorMessage = error;
            captureState.store (static_cast<int> (CaptureState::Idle));
            DBG ("startCapture failed: " + error);
            return;
        }
        captureErrorMessage.clear();
    }
}

void PresaProcessor::startCapture()
{
    captureBuffer.punch();
    captureState.store (static_cast<int> (CaptureState::Recording));
}

void PresaProcessor::stopCapture()
{
    captureBuffer.stop();
    captureState.store (static_cast<int> (CaptureState::Idle));

    // Loopback device stays open — no need to restart for next capture
}

CaptureState PresaProcessor::getCaptureState() const
{
    return static_cast<CaptureState> (captureState.load());
}

int PresaProcessor::getExportStart() const
{
    const int total = samplePlayer.getSampleLength();
    if (total <= 0) return 0;

    // SLICE mode always exports the full buffer so every pad's audio ends
    // up in the file; SAMPLE mode respects the live trim window.
    if (samplerMode.load() == SamplerMode::Slice)
        return 0;

    return juce::jlimit (0, total,
                         static_cast<int> (trimStart.load() * total));
}

int PresaProcessor::getExportEnd() const
{
    const int total = samplePlayer.getSampleLength();
    if (total <= 0) return 0;

    if (samplerMode.load() == SamplerMode::Slice)
        return total;

    return juce::jlimit (getExportStart(), total,
                         static_cast<int> (trimEnd.load() * total));
}

double PresaProcessor::getCaptureSampleRate() const
{
    if (loopbackCapture != nullptr)
        return loopbackCapture->getCaptureSampleRate();
    return 0.0;
}

void PresaProcessor::setStatusMessage (const juce::String& msg)
{
    {
        const juce::ScopedLock sl (statusMessageLock);
        statusMessage = msg;
    }
    // Message fades after 3 s. Using millisecond counter rather than Time
    // because getMillisecondCounter is monotonic and cheap.
    const juce::int64 now = static_cast<juce::int64> (juce::Time::getMillisecondCounter());
    statusMessageDeadlineMs.store (now + 3000);
}

juce::String PresaProcessor::getStatusMessage() const
{
    const juce::int64 now = static_cast<juce::int64> (juce::Time::getMillisecondCounter());
    if (now >= statusMessageDeadlineMs.load())
        return {};

    const juce::ScopedLock sl (statusMessageLock);
    return statusMessage;
}

void PresaProcessor::setSamplerMode (SamplerMode m)
{
    if (samplerMode.load() == m) return;
    samplerMode.store (m);

    // Update the player's slice layout so an already-loaded sample re-maps
    // immediately; no re-capture required for the toggle to take effect.
    samplePlayer.setSliceLayout (m == SamplerMode::Sample
                                 ? SamplePlayer::SliceLayout::FullRange
                                 : SamplePlayer::SliceLayout::Even);

    // Re-centre per-pad root notes so the pitch math matches the new mode:
    //   Slice  → pad N plays at native pitch when triggered at its own note
    //   Sample → every pad shares a central root (kSampleModeCenterRoot),
    //            so lower pads play slower/darker and higher pads faster/brighter
    for (int i = 0; i < kNumPads; ++i)
    {
        pads[i].rootNote = (m == SamplerMode::Sample)
                         ? kSampleModeCenterRoot
                         : kMidiBase + i;
    }

    // Reapply the current trim window under the new mode's mapping. This
    // also mirrors regions into pads[i].sliceStart/End.
    setTrimRange (trimStart.load(), trimEnd.load());

    // Nudge the UI to refresh its cached waveform/slice display.
    newSampleReady.store (true);
}

void PresaProcessor::setTrimRange (float startNorm, float endNorm)
{
    // Clamp and enforce a minimum window so start never reaches end.
    constexpr float kMinWindow = 0.001f;
    startNorm = juce::jlimit (0.0f, 1.0f - kMinWindow, startNorm);
    endNorm   = juce::jlimit (startNorm + kMinWindow, 1.0f, endNorm);

    trimStart.store (startNorm);
    trimEnd.store   (endNorm);

    const int total = samplePlayer.getSampleLength();
    if (total <= 0)
    {
        // No sample loaded yet — still update the PadState mirror so the
        // waveform reflects the chosen trim for when a capture lands.
        for (int i = 0; i < kNumPads; ++i)
        {
            pads[i].sliceStart = startNorm;
            pads[i].sliceEnd   = endNorm;
        }
        return;
    }

    const int sStart = juce::jlimit (0, total, static_cast<int> (startNorm * total));
    const int sEnd   = juce::jlimit (sStart, total, static_cast<int> (endNorm   * total));

    if (samplerMode.load() == SamplerMode::Sample)
    {
        // Every pad plays the same trimmed region.
        for (int i = 0; i < kNumPads; ++i)
        {
            samplePlayer.setPadSlice (i, sStart, sEnd);
            pads[i].sliceStart = startNorm;
            pads[i].sliceEnd   = endNorm;
        }
    }
    else
    {
        // Slice mode: 16 equal chunks inside the trim window.
        const int windowLen = sEnd - sStart;
        const int chunkLen  = juce::jmax (1, windowLen / kNumPads);

        for (int i = 0; i < kNumPads; ++i)
        {
            int chunkStart = sStart + i * chunkLen;
            int chunkEnd   = (i == kNumPads - 1) ? sEnd : (chunkStart + chunkLen);
            samplePlayer.setPadSlice (i, chunkStart, chunkEnd);

            pads[i].sliceStart = static_cast<float> (chunkStart) / total;
            pads[i].sliceEnd   = static_cast<float> (chunkEnd)   / total;
        }
    }
}

void PresaProcessor::prepareToStop()
{
    if (loopbackCapture)
    {
        loopbackCapture->stopCapture();
        loopbackCapture.reset();
    }
}

// ── Pad triggers from UI ───────────────────────────────────────────────────

void PresaProcessor::triggerPadFromUI (int padIndex)
{
    if (padIndex >= 0 && padIndex < kNumPads)
    {
        int midiNote = kMidiBase + padIndex;
        samplePlayer.noteOn (padIndex, midiNote, pads[padIndex].rootNote);
    }
}

void PresaProcessor::releasePadFromUI (int padIndex)
{
    if (padIndex >= 0 && padIndex < kNumPads)
        samplePlayer.noteOff (padIndex);
}

// ── SAMPLE-mode global playback ────────────────────────────────────────────
//
// Pad 0 is used as the canonical voice for SAMPLE mode. Its slice region is
// kept in sync with the trim window by setTrimRange, so playback respects the
// user's trim handles. midiNote is resolved against kSampleModeRootMidi (60)
// so C4 plays at native pitch.

void PresaProcessor::triggerSamplePlayback (int midiNote)
{
    samplePlayer.noteOn (0, midiNote, kSampleModeRootMidi);
}

void PresaProcessor::triggerSamplePlaybackFromPosition (int midiNote,
                                                        float normalizedPosition)
{
    const int total = samplePlayer.getSampleLength();
    if (total <= 0)
        return;

    const int startSample = juce::jlimit (0, total - 1,
                                          static_cast<int> (normalizedPosition * total));
    samplePlayer.noteOn (0, midiNote, kSampleModeRootMidi, startSample);
}

void PresaProcessor::stopSamplePlayback()
{
    samplePlayer.noteOff (0);
}

// ── Editor ─────────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* PresaProcessor::createEditor()
{
    return new PresaEditor (*this);
}

// ── State persistence ──────────────────────────────────────────────────────

void PresaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = std::make_unique<juce::XmlElement> ("PresaState");
    xml->setAttribute ("version", 3);
    xml->setAttribute ("selectedPad", selectedPad.load());
    xml->setAttribute ("samplerMode", static_cast<int> (samplerMode.load()));

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* padXml = xml->createNewChildElement ("Pad");
        padXml->setAttribute ("index",      i);
        padXml->setAttribute ("dotX",       pads[i].dotX);
        padXml->setAttribute ("dotY",       pads[i].dotY);
        padXml->setAttribute ("mode",       static_cast<int> (pads[i].mode));
        padXml->setAttribute ("sliceStart", pads[i].sliceStart);
        padXml->setAttribute ("sliceEnd",   pads[i].sliceEnd);
        padXml->setAttribute ("rootNote",   pads[i].rootNote);
    }

    copyXmlToBinary (*xml, destData);
}

void PresaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName ("PresaState"))
        return;

    selectedPad.store (xml->getIntAttribute ("selectedPad", 0));

    // Version 1 stored a blanket rootNote = 60 (C4) for every pad, which made
    // pads triggered at 36..51 play back 2+ octaves below root (0.25x speed,
    // perceived as severe stretch). Version 2 defaults each pad's root to its
    // own trigger note. When loading v<2, ignore saved rootNotes.
    const int savedVersion = xml->getIntAttribute ("version", 1);
    const bool migrateRootNotes = (savedVersion < 2);

    // Sampler mode was introduced in v3. Older saves imply Slice mode.
    const int savedMode = xml->getIntAttribute ("samplerMode",
                                                static_cast<int> (SamplerMode::Slice));
    samplerMode.store (static_cast<SamplerMode> (savedMode));
    samplePlayer.setSliceLayout (samplerMode.load() == SamplerMode::Sample
                                 ? SamplePlayer::SliceLayout::FullRange
                                 : SamplePlayer::SliceLayout::Even);

    for (auto* padXml : xml->getChildWithTagNameIterator ("Pad"))
    {
        int idx = padXml->getIntAttribute ("index", -1);
        if (idx < 0 || idx >= kNumPads) continue;

        pads[idx].dotX       = (float) padXml->getDoubleAttribute ("dotX", 0.5);
        pads[idx].dotY       = (float) padXml->getDoubleAttribute ("dotY", 0.5);
        pads[idx].mode       = static_cast<PadMode> (padXml->getIntAttribute ("mode", 0));
        pads[idx].sliceStart = (float) padXml->getDoubleAttribute ("sliceStart", 0.0);
        pads[idx].sliceEnd   = (float) padXml->getDoubleAttribute ("sliceEnd", 1.0);

        pads[idx].rootNote = migrateRootNotes
                           ? kMidiBase + idx
                           : padXml->getIntAttribute ("rootNote", kMidiBase + idx);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PresaProcessor();
}
