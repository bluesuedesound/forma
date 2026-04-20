#include "SamplePlayer.h"

static void spLog (const juce::String& s)
{
    if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
    { fprintf (f, "%s\n", s.toRawUTF8()); fclose (f); }
}

SamplePlayer::SamplePlayer() {}

void SamplePlayer::prepare (double sampleRate, int /*blockSize*/)
{
    hostSampleRate = sampleRate;
    spLog ("SamplePlayer::prepare hostSampleRate=" + juce::String (hostSampleRate, 2));
}

void SamplePlayer::setSample (juce::AudioBuffer<float>&& newSample, int sampleRate)
{
    spLog ("SamplePlayer::setSample IN - " + juce::String (newSample.getNumSamples())
           + " samples, " + juce::String (newSample.getNumChannels()) + " chans");

    sampleBuffer = std::move (newSample);
    sampleLength = sampleBuffer.getNumSamples();
    sampleOriginalRate = sampleRate;

    for (auto& v : voices)
        v.active = false;

    recomputeSlices();

    spLog ("SamplePlayer::setSample OUT - sampleLength=" + juce::String (sampleLength)
           + " slices[0]=[" + juce::String (slices[0].start) + ".."
           + juce::String (slices[0].end) + "]");
}

void SamplePlayer::recomputeSlices()
{
    if (sampleLength == 0)
    {
        for (auto& s : slices)
            s = { 0, 0 };
        return;
    }

    if (sliceLayout == SliceLayout::FullRange)
    {
        for (int i = 0; i < kNumPads; ++i)
            slices[i] = { 0, sampleLength };
        return;
    }

    int sliceLen = sampleLength / kNumPads;
    for (int i = 0; i < kNumPads; ++i)
    {
        slices[i].start = i * sliceLen;
        slices[i].end   = (i == kNumPads - 1) ? sampleLength : (i + 1) * sliceLen;
    }
}

void SamplePlayer::setPadSlice (int padIndex, int startSample, int endSample)
{
    if (padIndex < 0 || padIndex >= kNumPads) return;

    const int maxLen = sampleLength;
    int s = juce::jlimit (0, maxLen, startSample);
    int e = juce::jlimit (s,  maxLen, endSample);

    slices[padIndex].start = s;
    slices[padIndex].end   = e;

    for (auto& v : voices)
    {
        if (v.active && v.padIndex == padIndex)
        {
            if (v.playhead < (double) s || v.playhead >= (double) e)
            {
                v.active    = false;
                v.releasing = false;
                v.envLevel  = 0.0f;
                v.envStage  = Voice::EnvStage::Off;
            }
        }
    }
}

void SamplePlayer::setSliceLayout (SliceLayout layout)
{
    if (sliceLayout == layout) return;
    sliceLayout = layout;

    for (auto& v : voices)
        v.active = false;

    recomputeSlices();
}

SamplePlayer::SliceRegion SamplePlayer::getSlice (int padIndex) const
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return { 0, 0 };
    return slices[padIndex];
}

void SamplePlayer::setPitchSemitones (int semitones)
{
    // Snap (already int) and clamp to the requested range.
    pitchOffsetSemitones.store (juce::jlimit (-24, 24, semitones));
}

void SamplePlayer::setSpeedMultiplier (float speed)
{
    speedMultiplier.store (juce::jlimit (0.25f, 4.0f, speed));
}

int SamplePlayer::findFreeVoice (int padIndex) const
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[i].padIndex == padIndex)
            return i;

    for (int i = 0; i < kMaxVoices; ++i)
        if (!voices[i].active)
            return i;

    int best = 0;
    float lowest = voices[0].envLevel;
    for (int i = 1; i < kMaxVoices; ++i)
    {
        if (voices[i].envLevel < lowest)
        {
            lowest = voices[i].envLevel;
            best = i;
        }
    }
    return best;
}

void SamplePlayer::noteOn (int padIndex, int midiNote, int rootNote)
{
    if (sampleLength == 0 || padIndex < 0 || padIndex >= kNumPads)
    {
        spLog ("SamplePlayer::noteOn REJECTED - sampleLength=" + juce::String (sampleLength)
             + " padIndex=" + juce::String (padIndex)
             + " hostSR=" + juce::String (hostSampleRate, 2)
             + " srcSR=" + juce::String (sampleOriginalRate));
        return;
    }

    // Monophonic retrigger — any new hit cuts every other active voice.
    for (auto& v : voices)
    {
        if (v.active)
        {
            v.active    = false;
            v.releasing = false;
            v.envLevel  = 0.0f;
            v.envStage  = Voice::EnvStage::Off;
        }
    }

    spLog ("SamplePlayer::noteOn pad=" + juce::String (padIndex)
           + " sampleLength=" + juce::String (sampleLength)
           + " slice=[" + juce::String (slices[padIndex].start) + ".."
           + juce::String (slices[padIndex].end) + "]"
           + " midi=" + juce::String (midiNote)
           + " root=" + juce::String (rootNote));

    int vi = findFreeVoice (padIndex);
    auto& v = voices[vi];

    v.padIndex = padIndex;

    // Start from slice end when reversed so the voice plays toward the start.
    const auto& slice = slices[padIndex];
    v.playhead = reversed.load()
               ? static_cast<double> (juce::jmax (slice.start, slice.end - 1))
               : static_cast<double> (slice.start);

    v.basePitchRatio = std::pow (2.0, (midiNote - rootNote) / 12.0)
                     * (static_cast<double> (sampleOriginalRate) / hostSampleRate);
    v.active     = true;
    v.releasing  = false;

    float attackSec  = 0.005f;
    float releaseSec = 0.05f;
    v.attackRate  = 1.0f / (float) (hostSampleRate * attackSec);
    v.releaseRate = 1.0f / (float) (hostSampleRate * releaseSec);
    v.envLevel    = 0.0f;
    v.envStage    = Voice::EnvStage::Attack;
}

void SamplePlayer::noteOff (int padIndex)
{
    for (auto& v : voices)
    {
        if (v.active && v.padIndex == padIndex && !v.releasing)
        {
            v.releasing = true;
            v.envStage  = Voice::EnvStage::Release;
        }
    }
}

void SamplePlayer::processBlock (juce::AudioBuffer<float>& output, int numSamples)
{
    if (sampleLength == 0) return;

    const int outCh = output.getNumChannels();
    const int srcCh = sampleBuffer.getNumChannels();

    // Live params snapshotted once per block.
    const int   pitchSemi = pitchOffsetSemitones.load();
    const float speedMul  = speedMultiplier.load();
    const bool  loopOn    = looping.load();
    const bool  revOn     = reversed.load();

    const double pitchScale = std::pow (2.0, pitchSemi / 12.0);
    const double liveScale  = pitchScale * (double) speedMul;

    for (auto& v : voices)
    {
        if (!v.active) continue;

        if (!hasLoggedFirstPlayback)
        {
            hasLoggedFirstPlayback = true;
            spLog ("SamplePlayer: first playback pad=" + juce::String (v.padIndex));
        }

        const auto& slice = slices[v.padIndex];
        const int   sliceStart = slice.start;
        const int   sliceEnd   = slice.end;
        const int   sliceLen   = sliceEnd - sliceStart;

        if (sliceLen <= 1)
        {
            v.active = false;
            continue;
        }

        const int  xfadeLen  = juce::jmin (kLoopXfadeSamples, sliceLen / 2);
        const bool useXfade  = loopOn && xfadeLen > 1;

        const double perSampleRatio = v.basePitchRatio * liveScale;
        const double step = revOn ? -perSampleRatio : perSampleRatio;

        for (int i = 0; i < numSamples; ++i)
        {
            // ── Envelope ───────────────────────────────────────────────
            switch (v.envStage)
            {
                case Voice::EnvStage::Attack:
                    v.envLevel += v.attackRate;
                    if (v.envLevel >= 1.0f)
                    {
                        v.envLevel = 1.0f;
                        v.envStage = Voice::EnvStage::Sustain;
                    }
                    break;
                case Voice::EnvStage::Sustain:
                    break;
                case Voice::EnvStage::Release:
                    v.envLevel -= v.releaseRate;
                    if (v.envLevel <= 0.0f)
                    {
                        v.envLevel = 0.0f;
                        v.active = false;
                        v.envStage = Voice::EnvStage::Off;
                        goto nextVoice;
                    }
                    break;
                case Voice::EnvStage::Off:
                    v.active = false;
                    goto nextVoice;
            }

            // ── Read samples (with optional loop crossfade) ───────────
            auto readFrame = [&] (double pos, float out[2])
            {
                int p0 = (int) pos;
                int p1 = p0 + 1;
                float frac = (float) (pos - (double) p0);

                p0 = juce::jlimit (sliceStart, sliceEnd - 1, p0);
                p1 = juce::jlimit (sliceStart, sliceEnd - 1, p1);

                for (int c = 0; c < 2; ++c)
                {
                    const int sc = juce::jmin (c, srcCh - 1);
                    const float s0 = sampleBuffer.getSample (sc, p0);
                    const float s1 = sampleBuffer.getSample (sc, p1);
                    out[c] = s0 + (s1 - s0) * frac;
                }
            };

            float tail[2] = { 0.0f, 0.0f };
            float head[2] = { 0.0f, 0.0f };
            float mixed[2] = { 0.0f, 0.0f };

            if (!revOn)
            {
                // Forward playback
                const double xfadeEdge = (double) (sliceEnd - xfadeLen);

                if (useXfade && v.playhead >= xfadeEdge && v.playhead < (double) sliceEnd)
                {
                    const float fadeOut = (float) (((double) sliceEnd - v.playhead)
                                                   / (double) xfadeLen);
                    const float fadeIn  = 1.0f - fadeOut;

                    const double headPos = (double) sliceStart
                                         + (v.playhead - xfadeEdge);

                    readFrame (v.playhead, tail);
                    readFrame (headPos,    head);

                    mixed[0] = tail[0] * fadeOut + head[0] * fadeIn;
                    mixed[1] = tail[1] * fadeOut + head[1] * fadeIn;
                }
                else if (v.playhead >= (double) sliceStart
                      && v.playhead <  (double) sliceEnd)
                {
                    readFrame (v.playhead, mixed);
                }
            }
            else
            {
                // Reversed playback — mirror the crossfade logic around sliceStart.
                const double xfadeEdge = (double) (sliceStart + xfadeLen);

                if (useXfade && v.playhead <= xfadeEdge
                             && v.playhead >  (double) sliceStart)
                {
                    const float fadeOut = (float) ((v.playhead - (double) sliceStart)
                                                    / (double) xfadeLen);
                    const float fadeIn  = 1.0f - fadeOut;

                    const double headPos = (double) sliceEnd
                                         - (xfadeEdge - v.playhead);

                    readFrame (v.playhead, tail);
                    readFrame (headPos,    head);

                    mixed[0] = tail[0] * fadeOut + head[0] * fadeIn;
                    mixed[1] = tail[1] * fadeOut + head[1] * fadeIn;
                }
                else if (v.playhead >  (double) sliceStart
                      && v.playhead <= (double) (sliceEnd - 1))
                {
                    readFrame (v.playhead, mixed);
                }
            }

            for (int c = 0; c < outCh; ++c)
                output.addSample (c, i, mixed[c] * v.envLevel);

            // ── Advance playhead + wrap/terminate ─────────────────────
            v.playhead += step;

            if (!revOn)
            {
                if (v.playhead >= (double) sliceEnd)
                {
                    if (loopOn)
                    {
                        const double overshoot = v.playhead - (double) sliceEnd;
                        // Jump past the xfade region — the head side has already
                        // been playing there concurrently, so continuing from
                        // sliceStart + xfadeLen preserves phase continuity.
                        v.playhead = (double) sliceStart
                                   + (double) xfadeLen
                                   + overshoot;
                    }
                    else
                    {
                        v.active = false;
                        break;
                    }
                }
            }
            else
            {
                if (v.playhead <= (double) sliceStart)
                {
                    if (loopOn)
                    {
                        const double overshoot = (double) sliceStart - v.playhead;
                        v.playhead = (double) (sliceEnd - 1)
                                   - (double) xfadeLen
                                   - overshoot;
                    }
                    else
                    {
                        v.active = false;
                        break;
                    }
                }
            }
        }
        nextVoice:;
    }
}
