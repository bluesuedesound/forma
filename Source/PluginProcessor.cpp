#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "FormaSynthPresets.h"

int FormaProcessor::pitchClassToDegree (int pitchClass)
{
    switch (pitchClass)
    {
        case 0:  return 1;  case 2:  return 2;  case 4:  return 3;
        case 5:  return 4;  case 7:  return 5;  case 9:  return 6;
        case 11: return 7;  default: return 0;
    }
}

FormaProcessor::FormaProcessor()
    : AudioProcessor (BusesProperties()
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    currentChordNotes.reserve (8);
    prevChordNotes.reserve (8);
    pendingNotes.reserve (32);
    recentDegrees.reserve (8);

    // Initialize two synth engines
    chordSynth.addSound (new FormaSound());
    for (int i = 0; i < 8; ++i) chordSynth.addVoice (new FormaVoice());

    bassSynth.addSound (new FormaSound());
    for (int i = 0; i < 2; ++i) bassSynth.addVoice (new FormaVoice());

    // Apply default sound preset
    applySoundPreset (0);

    // Load suggestion model from embedded binary data
    auto jsonStr = juce::String::fromUTF8 (BinaryData::forma_suggestions_json,
                                            BinaryData::forma_suggestions_jsonSize);
    auto parsed = juce::JSON::parse (jsonStr);
    suggestionEngine.loadModel (parsed);
    suggestionEngine.loadZonesFromBinaryData();

    // Factory presets
    {
        auto& p = presets[0];
        p.name = "late night soul"; p.mood = "Deep"; p.keyRoot = 0;
        p.colorAmount = 0.75f; p.feelAmount = 0.5f;
        p.xyDotX = 0.5f; p.xyDotY = 0.25f;
        p.bassOctave = -1; p.bassMode = 0; p.bassTriggerNote = 0; p.bassVariation = 0.30f;
        p.chordsEnabled = true; p.bassEnabled = true;
        p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
    {
        auto& p = presets[1];
        p.name = "golden hour"; p.mood = "Bright"; p.keyRoot = 7; // G
        p.colorAmount = 0.5f; p.feelAmount = 0.3f;
        p.xyDotX = 0.3f; p.xyDotY = 0.5f;
        p.bassOctave = -1; p.bassMode = 0; p.bassTriggerNote = 0; p.bassVariation = 0.30f;
        p.chordsEnabled = true; p.bassEnabled = true;
        p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
    {
        auto& p = presets[2];
        p.name = "Dream Wash"; p.mood = "Dream"; p.keyRoot = 5; // F
        p.colorAmount = 0.7f; p.feelAmount = 0.6f;
        p.xyDotX = 0.6f; p.xyDotY = 0.3f;
        p.bassOctave = -1; p.bassMode = 0; p.bassTriggerNote = 0; p.bassVariation = 0.30f;
        p.chordsEnabled = true; p.bassEnabled = true;
        p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
    {
        auto& p = presets[3];
        p.name = "Tender Moment"; p.mood = "Tender"; p.keyRoot = 4; // E
    }
    {
        auto& p = presets[4];
        p.name = "soul dusk"; p.mood = "Dusk"; p.keyRoot = 0; // C
        p.colorAmount = 0.55f; p.feelAmount = 0.50f;
        p.xyDotX = 0.50f; p.xyDotY = 0.45f;
        p.bassOctave = -1; p.bassMode = 0; p.bassTriggerNote = 0; p.bassVariation = 0.30f;
        p.chordsEnabled = true; p.bassEnabled = true;
        p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
}

void FormaProcessor::applySoundPreset (int preset)
{
    using namespace FormaSynthPresets;

    FormaVoiceParams chordP, bassP;
    switch (preset)
    {
        case 0: chordP = Keys();      bassP = Sub();     break; // Keys
        case 1: chordP = Felt();      bassP = Rubber();  break; // Felt
        case 2: chordP = Glass();     bassP = Sub();     break; // Glass
        case 3: chordP = Tape();      bassP = Vintage(); break; // Tape
        case 4: chordP = PadPreset(); bassP = Sub();     break; // Ambient
        case 5: chordP = Keys();      bassP = Sub();     break; // Mallet
        default: chordP = Keys();     bassP = Sub();     break;
    }

    for (int i = 0; i < chordSynth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<FormaVoice*> (chordSynth.getVoice (i)))
            v->params = chordP;
    for (int i = 0; i < bassSynth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<FormaVoice*> (bassSynth.getVoice (i)))
            v->params = bassP;

    currentSoundPreset.store (preset);
}

FormaProcessor::~FormaProcessor()
{
    resetPlayingState();
}

// ── State persistence ──────────────────────────────────────────────────────

void FormaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = std::make_unique<juce::XmlElement> ("FormaState");
    xml->setAttribute ("version", 2);

    // Harmony
    xml->setAttribute ("mood", harmonyEngine.getCurrentMood());
    xml->setAttribute ("key", harmonyEngine.getRootMidi() - 48);
    xml->setAttribute ("color", (double) colorAmount.load());
    xml->setAttribute ("feel",  (double) feelAmount.load());
    xml->setAttribute ("xyDotX", (double) xyDotX.load());
    xml->setAttribute ("xyDotY", (double) xyDotY.load());

    // Voice enable toggles + bass controls
    xml->setAttribute ("chordsEnabled",   chordsEnabled.load());
    xml->setAttribute ("bassEnabled",     bassEnabled.load());
    xml->setAttribute ("bassOct",         octaveBassParam.load());
    xml->setAttribute ("bassMode",        bassMode.load());
    xml->setAttribute ("bassTrigNote",    bassTriggerNoteParam.load());
    xml->setAttribute ("bassVariation",   (double) bassVariationAmount.load());

    // Sync
    xml->setAttribute ("syncMode",   syncMode.load());

    // Synth
    xml->setAttribute ("synthVol", (double) synthVolume.load());
    xml->setAttribute ("chordOctave", octaveChordParam.load());
    xml->setAttribute ("soundPreset", currentSoundPreset.load());
    xml->setAttribute ("voicingMode", voicingMode.load());

    // Suggestions
    xml->setAttribute ("suggestionsVisible", suggestionsVisible.load());

    // Presets
    for (int i = 0; i < NUM_PRESETS; ++i)
    {
        auto* pe = xml->createNewChildElement ("Preset");
        pe->setAttribute ("slot",  i);
        pe->setAttribute ("empty", presets[i].isEmpty);
        if (!presets[i].isEmpty)
        {
            pe->setAttribute ("name",    presets[i].name);
            pe->setAttribute ("mood",    presets[i].mood);
            pe->setAttribute ("key",     presets[i].keyRoot);
            pe->setAttribute ("color",   (double) presets[i].colorAmount);
            pe->setAttribute ("feel",    (double) presets[i].feelAmount);
            pe->setAttribute ("dotX",    (double) presets[i].xyDotX);
            pe->setAttribute ("dotY",    (double) presets[i].xyDotY);
            pe->setAttribute ("bassOct",  presets[i].bassOctave);
            pe->setAttribute ("bassMode", presets[i].bassMode);
            pe->setAttribute ("bassTrigNote", presets[i].bassTriggerNote);
            pe->setAttribute ("bassVar",  (double) presets[i].bassVariation);
            pe->setAttribute ("chordsOn", presets[i].chordsEnabled);
            pe->setAttribute ("bassOn",   presets[i].bassEnabled);
            pe->setAttribute ("sync",     presets[i].syncMode);
            pe->setAttribute ("synth",    (double) presets[i].synthVolume);
        }
    }
    xml->setAttribute ("currentPreset", currentPresetIndex);

    copyXmlToBinary (*xml, destData);
}

void FormaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName ("FormaState")) return;

    // Release any active notes before restoring state
    resetPlayingState();

    // Harmony
    juce::String mood = xml->getStringAttribute ("mood", "Bright");
    harmonyEngine.setMood (mood);
    int keyI = xml->getIntAttribute ("key", 0);
    harmonyEngine.setKey (48 + juce::jlimit (0, 11, keyI));
    colorAmount.store ((float) xml->getDoubleAttribute ("color", 0.5));
    feelAmount.store  ((float) xml->getDoubleAttribute ("feel",  0.0));
    harmonyEngine.setColorAmount (colorAmount.load());
    xyDotX.store ((float) xml->getDoubleAttribute ("xyDotX", 0.5));
    xyDotY.store ((float) xml->getDoubleAttribute ("xyDotY", 0.5));

    // Voice enable toggles. Old sessions may not have these — default both
    // on so reopening an old file is sensible (both voices audible).
    chordsEnabled.store (xml->getBoolAttribute ("chordsEnabled", true));
    bassEnabled.store   (xml->getBoolAttribute ("bassEnabled",   true));

    // Bass controls
    octaveBassParam.store  (xml->getIntAttribute  ("bassOct", -1));
    bassMode.store (juce::jlimit (0, 2, xml->getIntAttribute ("bassMode", 0)));
    bassTriggerNoteParam.store (juce::jlimit (0, 127,
        xml->getIntAttribute ("bassTrigNote", 0)));
    bassVariationAmount.store ((float) juce::jlimit (0.0,  1.0,
        xml->getDoubleAttribute ("bassVariation", 0.30)));

    // Sync
    syncMode.store (xml->getIntAttribute ("syncMode", 2));

    // Synth
    synthVolume.store ((float) xml->getDoubleAttribute ("synthVol", 0.7));
    octaveChordParam.store (xml->getIntAttribute ("chordOctave", 0));
    applySoundPreset (xml->getIntAttribute ("soundPreset", 0));
    voicingMode.store (xml->getIntAttribute ("voicingMode", 0));

    // Suggestions
    suggestionsVisible.store (xml->getBoolAttribute ("suggestionsVisible", true));

    // Drift
    driftAmount.store (getDriftForMoodAndFeel (feelAmount.load()));

    // Presets
    for (auto* pe : xml->getChildWithTagNameIterator ("Preset"))
    {
        int slot = pe->getIntAttribute ("slot", -1);
        if (slot < 0 || slot >= NUM_PRESETS) continue;
        presets[slot].isEmpty = pe->getBoolAttribute ("empty", true);
        if (!presets[slot].isEmpty)
        {
            presets[slot].name        = pe->getStringAttribute ("name");
            presets[slot].mood        = pe->getStringAttribute ("mood");
            presets[slot].keyRoot     = pe->getIntAttribute ("key");
            presets[slot].colorAmount = (float) pe->getDoubleAttribute ("color");
            presets[slot].feelAmount  = (float) pe->getDoubleAttribute ("feel");
            presets[slot].xyDotX      = (float) pe->getDoubleAttribute ("dotX");
            presets[slot].xyDotY      = (float) pe->getDoubleAttribute ("dotY");
            presets[slot].bassOctave       = pe->getIntAttribute ("bassOct", -1);
            presets[slot].bassMode         = juce::jlimit (0, 2,
                                                pe->getIntAttribute ("bassMode", 0));
            presets[slot].bassTriggerNote  = juce::jlimit (0, 127,
                                                pe->getIntAttribute ("bassTrigNote", 0));
            presets[slot].bassVariation    = (float) juce::jlimit (0.0, 1.0,
                                                pe->getDoubleAttribute ("bassVar", 0.30));
            presets[slot].chordsEnabled    = pe->getBoolAttribute ("chordsOn", true);
            presets[slot].bassEnabled      = pe->getBoolAttribute ("bassOn",   true);
            presets[slot].syncMode         = pe->getIntAttribute ("sync", 2);
            presets[slot].synthVolume      = (float) pe->getDoubleAttribute ("synth", 0.7);
        }
    }
    currentPresetIndex = xml->getIntAttribute ("currentPreset", -1);

    // Sync UI on message thread
    juce::MessageManager::callAsync ([this]() {
        if (auto* ed = dynamic_cast<FormaEditor*> (getActiveEditor()))
            ed->syncUIFromProcessor();
    });
}

// ── Preset operations ─────────────────────────────────────────────────────

void FormaProcessor::savePreset (int slot, const juce::String& name)
{
    if (slot < 0 || slot >= NUM_PRESETS) return;
    auto& p = presets[slot];
    p.name            = name;
    p.mood            = harmonyEngine.getCurrentMood();
    p.keyRoot         = harmonyEngine.getRootMidi() - 48;
    p.colorAmount     = colorAmount.load();
    p.feelAmount      = feelAmount.load();
    p.xyDotX          = xyDotX.load();
    p.xyDotY          = xyDotY.load();
    p.bassOctave      = octaveBassParam.load();
    p.bassMode        = bassMode.load();
    p.bassTriggerNote = bassTriggerNoteParam.load();
    p.bassVariation   = bassVariationAmount.load();
    p.chordsEnabled   = chordsEnabled.load();
    p.bassEnabled     = bassEnabled.load();
    p.syncMode        = syncMode.load();
    p.synthVolume     = synthVolume.load();
    p.isEmpty         = false;
    currentPresetIndex = slot;
}

void FormaProcessor::loadPreset (int slot)
{
    if (slot < 0 || slot >= NUM_PRESETS) return;
    auto& p = presets[slot];
    if (p.isEmpty) return;

    resetPlayingState();

    harmonyEngine.setMood (p.mood);
    harmonyEngine.setKey (48 + juce::jlimit (0, 11, p.keyRoot));
    colorAmount.store (p.colorAmount);
    feelAmount.store (p.feelAmount);
    harmonyEngine.setColorAmount (p.colorAmount);
    xyDotX.store (p.xyDotX);
    xyDotY.store (p.xyDotY);
    octaveBassParam.store (p.bassOctave);
    bassMode.store (juce::jlimit (0, 2, p.bassMode));
    bassTriggerNoteParam.store (juce::jlimit (0, 127, p.bassTriggerNote));
    bassVariationAmount.store (juce::jlimit (0.0f, 1.0f, p.bassVariation));
    chordsEnabled.store (p.chordsEnabled);
    bassEnabled.store   (p.bassEnabled);
    syncMode.store (p.syncMode);
    synthVolume.store (p.synthVolume);
    driftAmount.store (getDriftForMoodAndFeel (p.feelAmount));
    currentPresetIndex = slot;

    juce::MessageManager::callAsync ([this]() {
        if (auto* ed = dynamic_cast<FormaEditor*> (getActiveEditor()))
            ed->syncUIFromProcessor();
    });
}

// ── Mood defaults ──────────────────────────────────────────────────────────

struct MoodDef {
    float feel, drift, color;
    int voicing;
};

static const MoodDef kMoodDefs[] = {
    // feel, drift, color, voicing
    { 0.30f, 0.15f, 0.50f,  0 },  // Bright
    { 0.40f, 0.35f, 0.60f, -1 },  // Warm
    { 0.60f, 0.25f, 0.70f,  3 },  // Dream
    { 0.50f, 0.45f, 0.75f, -1 },  // Deep
    { 0.15f, 0.10f, 0.40f,  1 },  // Hollow
    { 0.60f, 0.35f, 0.70f,  1 },  // Tender
    { 0.35f, 0.20f, 0.55f, -2 },  // Tense
    { 0.50f, 0.40f, 0.55f,  0 },  // Dusk
    // ── Bright Lights pack ──
    { 0.25f, 0.10f, 0.45f,  0 },  // Crest
    { 0.40f, 0.30f, 0.35f, -1 },  // Nocturne
    { 0.35f, 0.20f, 0.50f,  1 },  // Shimmer
    { 0.20f, 0.15f, 0.50f,  0 },  // Static
};

void FormaProcessor::applyMoodDefaults (int moodIndex)
{
    if (moodIndex < 0 || moodIndex >= 12) return;
    const auto& d = kMoodDefs[moodIndex];

    feelAmount.store (d.feel);
    driftAmount.store (d.drift);
    colorAmount.store (d.color);
    harmonyEngine.setColorAmount (d.color);

    voicingParam.store (d.voicing);

    // Reset voice leading so first chord in new mood is fresh
    harmonyEngine.resetVoiceLeadingState();
    prevChordNotes.clear();
}

float FormaProcessor::getDriftForMoodAndFeel (float feel)
{
    static const std::map<juce::String, float> ceilings = {
        { "Bright", 0.15f }, { "Warm", 0.35f }, { "Dream", 0.25f },
        { "Deep",   0.45f }, { "Hollow", 0.10f }, { "Tender", 0.40f },
        { "Tense",  0.20f }, { "Dusk", 0.40f },
        { "Crest", 0.10f }, { "Nocturne", 0.30f },
        { "Shimmer", 0.20f }, { "Static", 0.15f }
    };
    auto it = ceilings.find (harmonyEngine.getCurrentMood());
    float ceiling = (it != ceilings.end()) ? it->second : 0.2f;
    return feel * ceiling;
}

// ── Audio lifecycle ────────────────────────────────────────────────────────

void FormaProcessor::prepareToPlay (double sampleRate, int)
{
    resetPlayingState();
    currentSampleRate = sampleRate;

    bassEngine.prepareToPlay (sampleRate);
    bassEngine.setMode (bassMode.load());

    chordSynth.setCurrentPlaybackSampleRate (sampleRate);
    bassSynth.setCurrentPlaybackSampleRate (sampleRate);

    for (int i = 0; i < chordSynth.getNumVoices(); ++i)
        static_cast<FormaVoice*> (chordSynth.getVoice (i))->prepareToPlay (sampleRate, 0);
    for (int i = 0; i < bassSynth.getNumVoices(); ++i)
        static_cast<FormaVoice*> (bassSynth.getVoice (i))->prepareToPlay (sampleRate, 0);
}

void FormaProcessor::releaseResources()
{
    resetPlayingState();
}

void FormaProcessor::resetPlayingState()
{
    // Send all-notes-off on all channels via editorMidi so processBlock picks it up
    {
        const juce::SpinLock::ScopedLockType lock (editorMidiLock);
        editorMidi.addEvent (juce::MidiMessage::allNotesOff (kChordChannel), 0);
        editorMidi.addEvent (juce::MidiMessage::allNotesOff (kBassChannel), 0);
        for (int note : currentChordNotes)
        {
            editorMidi.addEvent (juce::MidiMessage::noteOff (kChordChannel, note), 0);
            editorMidi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        }
        if (currentBassNote >= 0)
        {
            editorMidi.addEvent (juce::MidiMessage::noteOff (kBassChannel, currentBassNote), 0);
            editorMidi.addEvent (juce::MidiMessage::noteOff (1, currentBassNote), 0);
        }
    }

    pendingNotes.clear();
    bassEngine.releaseChord();
    currentChordNotes.clear();
    currentBassNote = -1;
    triggeredBassPlaying = -1;
    currentDegree = -1;
    activeDegree.store (-1);
    std::memset (heldDegreeCounts, 0, sizeof (heldDegreeCounts));
    chordSynth.allNotesOff (0, true);
    bassSynth.allNotesOff (0, true);
    resetHarmonicState();
}

// ── Pending notes (feel strum queue) ───────────────────────────────────────

void FormaProcessor::processPendingNotes (juce::MidiBuffer& out, int blockSize)
{
    auto it = pendingNotes.begin();
    while (it != pendingNotes.end())
    {
        it->samplesRemaining -= blockSize;
        if (it->samplesRemaining <= 0)
        {
            int offset = juce::jmax (0, blockSize + it->samplesRemaining);
            if (it->isNoteOff)
            {
                out.addEvent (juce::MidiMessage::noteOff (it->channel, it->note), offset);
                chordSynthMidi.addEvent (juce::MidiMessage::noteOff (1, it->note), offset);
            }
            else
            {
                out.addEvent (juce::MidiMessage::noteOn (it->channel, it->note,
                              (juce::uint8) it->velocity), offset);
                chordSynthMidi.addEvent (juce::MidiMessage::noteOn (1, it->note,
                                    (juce::uint8) it->velocity), offset);
            }
            it = pendingNotes.erase (it);
        }
        else
            ++it;
    }
}

// ── processBlock ───────────────────────────────────────────────────────────

void FormaProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    currentBlockSize = buffer.getNumSamples();

    octaveChord = octaveChordParam.load();
    octaveBass  = octaveBassParam.load();
    harmonyEngine.setColorAmount (colorAmount.load());
    harmonyEngine.setVoicing (voicingParam.load());

    // Push bass params each block.
    bassEngine.setMode (bassMode.load());
    bassEngine.setOctaveOffset (octaveBassParam.load());
    bassEngine.setVariationAmount (bassVariationAmount.load());

    // Compute anchor threshold from BPM
    {
        double bpm = linkBpm.load();
        if (bpm < 30.0) bpm = 120.0;
        double msPerBeat = 60000.0 / bpm;
        anchorHoldThresholdMs = msPerBeat * 0.5;
    }

    // Transport tracking + beat position estimation
    if (auto* ph = getPlayHead())
    {
        auto posInfo = ph->getPosition();
        if (posInfo.hasValue())
        {
            bool isPlaying = posInfo->getIsPlaying();

            if (isPlaying)
            {
                if (auto ppq = posInfo->getPpqPosition())
                {
                    double ppqVal = *ppq;
                    if (ppqVal < lastKnownPpqPosition - 1.0)
                        resetHarmonicState();
                    lastKnownPpqPosition = ppqVal;
                    currentBeatPosition = (float) std::fmod (ppqVal, 4.0);
                }
            }
            else
            {
                // Transport stopped — estimate beat position from last chord press
                if (lastChordPressTimeMs > 0.0)
                {
                    double now = juce::Time::getMillisecondCounterHiRes();
                    double bpm = linkBpm.load();
                    if (bpm < 30.0) bpm = 120.0;
                    double msPerBeat = 60000.0 / bpm;
                    double msSincePress = now - lastChordPressTimeMs;
                    double estimatedBeats = msSincePress / msPerBeat;

                    if (estimatedBeats <= 4.0)
                        currentBeatPosition = (float) std::fmod (estimatedBeats, 4.0);
                    else
                        currentBeatPosition = -1.0f;  // unknown after 4 beats of silence
                }
                else
                    currentBeatPosition = -1.0f;
            }

            if (!isPlaying && transportWasPlaying)
                resetHarmonicState();

            transportWasPlaying = isPlaying;
        }
    }

    // Detect voice-toggle transitions — flush stuck notes on the affected
    // synth/channel when a voice is switched off.
    const bool curChords = chordsEnabled.load();
    const bool curBass   = bassEnabled.load();
    if (curChords != prevChordsEnabled || curBass != prevBassEnabled)
    {
        if (! curChords && prevChordsEnabled)
        {
            midiMessages.addEvent (juce::MidiMessage::allNotesOff (kChordChannel), 0);
            chordSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            chordSynth.allNotesOff (0, false);
        }
        if (! curBass && prevBassEnabled)
        {
            midiMessages.addEvent (juce::MidiMessage::allNotesOff (kBassChannel), 0);
            bassSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            bassSynth.allNotesOff (0, false);
            triggeredBassPlaying = -1;
        }
        prevChordsEnabled = curChords;
        prevBassEnabled   = curBass;
    }

    {
        const juce::SpinLock::ScopedLockType lock (editorMidiLock);
        for (const auto metadata : editorMidi)
            midiMessages.addEvent (metadata.getMessage(), metadata.samplePosition);
        editorMidi.clear();
    }

    juce::MidiBuffer output;
    chordSynthMidi.clear();
    bassSynthMidi.clear();

    processPendingNotes (output, currentBlockSize);

    // Safety: cap pending notes to prevent unbounded growth
    if (pendingNotes.size() > 32)
        pendingNotes.clear();

    // MIDI trigger only active when bass mode uses it (KickTrigger or
    // KickVariation). Root mode ignores trigger notes.
    const int  bassModeCached = bassMode.load();
    const bool triggerMode    = (bassModeCached >= 1);
    const int  triggerNote    = bassTriggerNoteParam.load();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        int pos  = metadata.samplePosition;

        // ── MIDI-triggered bass: intercept trigger note before chord processing ──
        if (triggerMode && (msg.isNoteOn() || msg.isNoteOff())
            && msg.getNoteNumber() == triggerNote)
        {
            const bool sendBass = curBass;
            const int  bassCh   = kBassChannel;
            const int  bassOff  = juce::jmin (pos, currentBlockSize - 1);

            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                // Release any currently-sounding triggered bass first
                if (triggeredBassPlaying >= 0)
                {
                    if (sendBass)
                    {
                        output.addEvent (juce::MidiMessage::noteOff (bassCh, triggeredBassPlaying), bassOff);
                        bassSynthMidi.addEvent (juce::MidiMessage::noteOff (1, triggeredBassPlaying), bassOff);
                    }
                    triggeredBassPlaying = -1;
                }
                if (bassEngine.isChordActive())
                {
                    // Kick+Variation needs beat position; Kick Trigger just plays root.
                    double beatPos = -1.0;
                    if (auto* ph = getPlayHead())
                    {
                        auto posInfo = ph->getPosition();
                        if (posInfo.hasValue())
                            if (auto ppq = posInfo->getPpqPosition())
                                beatPos = *ppq;
                    }
                    int note = bassEngine.chooseTriggerNote (beatPos, rng);
                    if (note >= 0)
                    {
                        juce::uint8 vel = msg.getVelocity();
                        if (sendBass)
                        {
                            output.addEvent (juce::MidiMessage::noteOn (bassCh, note, vel), bassOff);
                            bassSynthMidi.addEvent (juce::MidiMessage::noteOn (1, note, vel), bassOff);
                        }
                        triggeredBassPlaying = note;
                    }
                }
            }
            else  // note-off (or note-on with velocity 0)
            {
                if (triggeredBassPlaying >= 0)
                {
                    if (sendBass)
                    {
                        output.addEvent (juce::MidiMessage::noteOff (bassCh, triggeredBassPlaying), bassOff);
                        bassSynthMidi.addEvent (juce::MidiMessage::noteOff (1, triggeredBassPlaying), bassOff);
                    }
                    triggeredBassPlaying = -1;
                }
            }
            continue;
        }

        if (msg.isNoteOn())
        {
            int pitchClass = msg.getNoteNumber() % 12;
            int degree = pitchClassToDegree (pitchClass);
            if (degree == 0) continue;
            heldDegreeCounts[degree]++;
            if (degree != currentDegree)
                triggerChord (degree, msg.getVelocity(), output, pos);
        }
        else if (msg.isNoteOff())
        {
            int pitchClass = msg.getNoteNumber() % 12;
            int degree = pitchClassToDegree (pitchClass);
            if (degree == 0) continue;
            if (heldDegreeCounts[degree] > 0) heldDegreeCounts[degree]--;
            if (heldDegreeCounts[degree] == 0 && degree == currentDegree)
                releaseChord (output, pos);
        }
        else if (msg.isController())
        {
            auto sync = static_cast<SyncMode> (syncMode.load());
            if (sync != SyncMode::Free)
            {
                int cc = msg.getControllerNumber(), val = msg.getControllerValue();

                // Harmony CCs (applied in Full, Expressive, Harmonic)
                if (cc == 1 && val >= 1 && val <= 7)
                { if (val != currentDegree) { heldDegreeCounts[val] = 1; triggerChord (val, 80, output, pos); } }
                else if (cc == 2 && val < 12) { harmonyEngine.setKey (48 + val); prevChordNotes.clear(); }
                else if (cc == 3 && val < 7)  harmonyEngine.setMood (HarmonyEngine::moodNames[val]);

                // Color CC (applied in Full and Expressive)
                else if (cc == 4 && (sync == SyncMode::Full || sync == SyncMode::Expressive))
                {
                    colorAmount.store (val / 127.0f);
                    harmonyEngine.setColorAmount (val / 127.0f);
                }

                // Feel/Drift CCs (applied in Full only)
                else if (sync == SyncMode::Full)
                {
                    if (cc == 5) feelAmount.store (val / 127.0f);
                    else if (cc == 6) driftAmount.store (val / 127.0f);
                }
            }
            output.addEvent (msg, pos);
        }
        else
            output.addEvent (msg, pos);
    }

    // Render two synth engines — they sum into the buffer.
    chordSynth.renderNextBlock (buffer, chordSynthMidi, 0, currentBlockSize);
    bassSynth.renderNextBlock  (buffer, bassSynthMidi,  0, currentBlockSize);

    // Master gain + soft clipper
    float masterGain = synthVolume.load();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < currentBlockSize; ++i)
        {
            float x = data[i] * masterGain;
            data[i] = x / (1.0f + std::abs (x));
        }
    }
    midiMessages.swapWith (output);
}

// ── Harmonic anchor system ─────────────────────────────────────────────────

void FormaProcessor::commitAnchor (int degree)
{
    prevAnchorDegree = anchorDegree;
    anchorDegree = degree;

    anchorHistory.push_back (degree);
    if ((int) anchorHistory.size() > 6)
        anchorHistory.erase (anchorHistory.begin());

    if (degree == 1) phrasePosition = 0;
    else phrasePosition++;

    // Update recent degrees for progression detection
    recentDegrees.push_back (degree);
    if ((int) recentDegrees.size() > 8)
        recentDegrees.erase (recentDegrees.begin());

    detectLoop();
    recomputeSuggestions();

    currentProgressionName = suggestionEngine.detectProgression (recentDegrees);

#if JUCE_DEBUG
    juce::String hist;
    for (int h : anchorHistory) hist += juce::String (h) + ",";
    DBG ("=== Anchor committed: " + juce::String (anchorDegree)
         + " phrase=" + juce::String (phrasePosition)
         + " loop=" + juce::String (loopDetected ? "YES" : "no")
         + " history=[" + hist.trimCharactersAtEnd (",") + "]");
#endif
}

void FormaProcessor::detectLoop()
{
    int sz = (int) anchorHistory.size();
    loopDetected = false;

    // 2-chord loop: ABAB
    if (sz >= 4
        && anchorHistory[(size_t)(sz - 1)] == anchorHistory[(size_t)(sz - 3)]
        && anchorHistory[(size_t)(sz - 2)] == anchorHistory[(size_t)(sz - 4)])
        loopDetected = true;

    // 3-chord loop: ABCABC
    if (sz >= 6
        && anchorHistory[(size_t)(sz - 1)] == anchorHistory[(size_t)(sz - 4)]
        && anchorHistory[(size_t)(sz - 2)] == anchorHistory[(size_t)(sz - 5)]
        && anchorHistory[(size_t)(sz - 3)] == anchorHistory[(size_t)(sz - 6)])
        loopDetected = true;
}

void FormaProcessor::recomputeSuggestions()
{
    if (anchorDegree < 1)
    {
        primarySuggestion.store (-1);
        secondarySuggestion.store (-1);
        return;
    }

    suggestionEngine.updateZone (harmonyEngine.getCurrentMoodIndex(),
                                 colorAmount.load());

    auto sug = suggestionEngine.getSuggestions (
        harmonyEngine.getCurrentMood(),
        anchorDegree,
        prevAnchorDegree,
        linkBpm.load(),
        currentBeatPosition,
        phrasePosition,
        loopDetected,
        anchorHistory);

    primarySuggestion.store (sug.primary);
    secondarySuggestion.store (sug.secondary);

#if JUCE_DEBUG
    DBG ("    Suggestions: primary=" + juce::String (sug.primary)
         + " secondary=" + juce::String (sug.secondary));
#endif
}

void FormaProcessor::checkKeySuggestion()
{
    int viCount = 0;
    for (int d : anchorHistory)
        if (d == 6) viCount++;

    if (viCount >= (int) anchorHistory.size() / 2 && nonTonicChordCount >= 6)
    {
        auto mood = harmonyEngine.getCurrentMood();
        bool isMajor = (mood == "Bright" || mood == "Warm" || mood == "Dream");
        if (isMajor)
        {
            int relMinRoot = (harmonyEngine.getRootMidi() + 9) % 12;
            static const char* nn[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
            juce::String sugMood = (mood == "Bright") ? "Hollow" : (mood == "Warm") ? "Deep" : "Tender";
            keySuggestion = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " try " + nn[relMinRoot] + " " + sugMood;
            keySuggestionActive = true;
        }
    }
    else
    {
        keySuggestionActive = false;
        keySuggestion = juce::String();
    }
}

void FormaProcessor::resetHarmonicState()
{
    anchorDegree      = -1;
    prevAnchorDegree  = -1;
    lastPlayedDegree  = -1;
    phrasePosition    = 0;
    loopDetected      = false;
    anchorHistory.clear();
    recentDegrees.clear();
    nonTonicChordCount = 0;
    keySuggestionActive = false;
    keySuggestion = juce::String();

    primarySuggestion.store (-1);
    secondarySuggestion.store (-1);
    currentProgressionName = juce::String();

    DBG ("Harmonic state reset");
}

void FormaProcessor::manualHarmonicReset()
{
    resetHarmonicState();
    DBG ("Manual harmonic reset triggered");
}

// ── Trigger chord ──────────────────────────────────────────────────────────

void FormaProcessor::triggerChord (int degree, juce::uint8 inputVelocity,
                                    juce::MidiBuffer& out, int samplePosition)
{
    releaseChord (out, samplePosition);

    const bool sendChords_ = chordsEnabled.load();
    const bool sendBass_   = bassEnabled.load();
    float feel  = feelAmount.load();
    float drift = driftAmount.load();

    auto rawChord = harmonyEngine.getChord (degree);

#if JUCE_DEBUG
    {
        juce::String rc;
        for (int n : rawChord) rc += juce::String (n) + " ";
        DBG ("triggerChord: degree=" + juce::String (degree) + " getChord=[" + rc.trimEnd() + "]"
             + " prevChordNotes.size=" + juce::String ((int) prevChordNotes.size()));
    }
#endif

    // Voice leading: melodic nearest-note. Runs on every chord press that
    // has a predecessor — there is no feel-based gate any more. feel still
    // tempers the register-gravity term inside getBestInversion.
    harmonyEngine.setFeelAmount (feel);

    std::vector<int> voiced;
    if (prevChordNotes.empty())
    {
        // First chord: anchor at mood register.
        voiced = harmonyEngine.placeNearRegister (rawChord,
                     harmonyEngine.getTargetRegisterCenter());
#if JUCE_DEBUG
        DBG ("voicing: first chord, placeNearRegister");
#endif
    }
    else
    {
        voiced = harmonyEngine.getBestInversion (rawChord, prevChordNotes, feel, degree);
#if JUCE_DEBUG
        DBG ("voicing: getBestInversion (feel=" + juce::String (feel, 2)
             + ", degree=" + juce::String (degree) + ")");
#endif
    }

#if JUCE_DEBUG
    {
        juce::String vs;
        for (int n : voiced) vs += juce::String (n) + " ";
        DBG ("  -> voiced=[" + vs.trimEnd() + "]");
    }
#endif

    // Store full 4-voice result for voice leading context
    for (auto& n : voiced) n = juce::jlimit (21, 108, n);
    prevChordNotes = voiced;  // always 4 voices for voice leading
    harmonyEngine.commitVoicingToCache (degree, prevChordNotes);

    // Apply voicing mode (reduces voices for output)
    auto vm = static_cast<VoicingMode> (voicingMode.load());
    if (vm == VoicingMode::Upper && (int) voiced.size() >= 3)
    {
        // Remove lowest voice (root)
        voiced.erase (voiced.begin());
    }
    else if (vm == VoicingMode::Shell && (int) voiced.size() >= 2)
    {
        // Root + highest voice only (shell voicing)
        int root = voiced.front();
        int top  = voiced.back();
        voiced = { root, top };
    }

    int chordShift = octaveChord * 12;
    currentChordNotes.clear();
    for (int note : voiced)
        currentChordNotes.push_back (juce::jlimit (0, 127, note + chordShift));

    int n = (int) currentChordNotes.size();
    if (n == 0) return;

    // Velocity spread
    std::vector<int> velocities (n);
    for (int i = 0; i < n; ++i)
    {
        float pos = (n > 1) ? (float) i / (float)(n - 1) : 0.0f;
        float botVel = juce::jmap (feel, 0.0f, 1.0f, 80.0f, 100.0f);
        float topVel = juce::jmap (feel, 0.0f, 1.0f, 80.0f, 55.0f);
        float vel = juce::jmap (pos, 0.0f, 1.0f, botVel, topVel)
                    + (rng.nextFloat() - 0.5f) * feel * 8.0f;
        velocities[i] = (int) juce::jlimit (1.0f, 127.0f, vel);
    }

    // Drift
    if (degree == lastDriftDegree)
    {
        driftAccum[degree] += (rng.nextFloat() - 0.5f) * drift * 12.0f;
        driftAccum[degree] = juce::jlimit (-drift * 30.0f, drift * 30.0f, driftAccum[degree]);
    }
    else
    {
        for (int d = 1; d <= 7; ++d) driftAccum[d] *= 0.5f;
        lastDriftDegree = degree;
    }

    float chordDriftMs = rng.nextFloat() * drift * 20.0f + std::abs (driftAccum[degree]);
    double samplesPerMs = currentSampleRate / 1000.0;
    int driftSamples = (int)(chordDriftMs * samplesPerMs);

    // Schedule chord notes with strum
    const bool sendChords = sendChords_;
    const int  chordCh    = kChordChannel;
    int currentOffset = samplePosition + driftSamples;

    for (int i = 0; i < n; ++i)
    {
        int note = currentChordNotes[i];
        int vel  = velocities[i];

        if (sendChords)
        {
            if (currentOffset < currentBlockSize)
            {
                out.addEvent (juce::MidiMessage::noteOn (chordCh, note, (juce::uint8) vel), currentOffset);
                chordSynthMidi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), currentOffset);
            }
            else
                pendingNotes.push_back ({ note, vel, chordCh, currentOffset - currentBlockSize, false });
        }

        if (i < n - 1)
        {
            float gapMs = feel * feel * 42.5f * (0.8f + rng.nextFloat() * 0.4f)
                          * (1.0f + (rng.nextFloat() - 0.5f) * drift * 0.6f);
            currentOffset += (int)(gapMs * samplesPerMs);
        }
    }

    // Bass — always update chord state (currentBassNote + BassEngine chord
    // context) so non-Chords tabs can generate audio. Only the chord voice's
    // noteOn emission is gated by chordsEnabled above with sendChords.
    {
        int rootPc = rawChord[0] % 12;
        int lowestVoice = currentChordNotes.empty() ? 48 : currentChordNotes.front();
        int bassNote = lowestVoice - 12;
        while (bassNote % 12 != rootPc) bassNote--;
        if (bassNote < 24) bassNote += 12;
        bassNote = juce::jlimit (24, 60, bassNote);
        bassNote = juce::jlimit (21, 72, bassNote + octaveBass * 12);
        currentBassNote = bassNote;

        auto triadQ = harmonyEngine.getChordQuality (degree);
        int thirdIv = (triadQ == "m" || triadQ == "d") ? 3 : 4;
        int fifthIv = (triadQ == "d") ? 6 : (triadQ == "A" ? 8 : 7);
        bassEngine.setCurrentChord (degree, currentBassNote, fifthIv, thirdIv);

        // Inline root bass only when Bass tab is active, bassMode == Root,
        // and MIDI trigger mode is off. All other bass modes are emitted by
        // BassEngine::process() from processBlock.
        // Inline fire only when bassMode == Root. Kick modes fire from the
        // MIDI trigger path in processBlock.
        if (sendBass_ && bassEngine.firesInlineOnChordPress())
        {
            int bassCh = kBassChannel;
            int bassOffset = juce::jmin (samplePosition, currentBlockSize - 1);
            out.addEvent (juce::MidiMessage::noteOn (bassCh, currentBassNote, (juce::uint8) 70), bassOffset);
            bassSynthMidi.addEvent (juce::MidiMessage::noteOn (1, currentBassNote, (juce::uint8) 70), bassOffset);
        }
    }

    currentDegree = degree;
    activeDegree.store (degree);
    lastChordVelocity.store ((float) inputVelocity / 127.0f);
    currentChordName = harmonyEngine.getChordName (degree);

    // ── Anchor system: record press time ──
    lastChordPressTimeMs = juce::Time::getMillisecondCounterHiRes();
    lastPlayedDegree = degree;

    // Update beat position for suggestions
    if (auto* ph = getPlayHead())
    {
        auto posInfo = ph->getPosition();
        if (posInfo.hasValue())
        {
            if (auto b = posInfo->getBpm()) linkBpm.store (*b);
            if (posInfo->getIsPlaying())
            {
                if (auto ppq = posInfo->getPpqPosition())
                    currentBeatPosition = (float) std::fmod (*ppq, 4.0);
            }
            else
                currentBeatPosition = 0.0f;  // chord press = beat 1 in free play
        }
    }

    // Immediately commit anchor for first chord (no prior context to wait for)
    if (anchorDegree < 0)
        commitAnchor (degree);

    // Key suggestion tracking
    if (degree == 1) { nonTonicChordCount = 0; keySuggestionActive = false; keySuggestion = juce::String(); }
    else nonTonicChordCount++;
    if (nonTonicChordCount >= 6) checkKeySuggestion();

    // Update register drift tracking on the canonical 4-voice voicing
    // (pre-octave-shift, pre-voicing-mode-reduction). currentChordNotes has
    // chordShift applied and may have voices removed by Upper/Shell modes,
    // both of which would skew the drift estimate.
    harmonyEngine.updateDriftTracking (prevChordNotes);

    // Immediately update suggestion display on every press
    // (anchor commit in releaseChord updates Markov state later)
    {
        suggestionEngine.updateZone (harmonyEngine.getCurrentMoodIndex(),
                                     colorAmount.load());

        auto sug = suggestionEngine.getSuggestions (
            harmonyEngine.getCurrentMood(),
            degree,              // use pressed degree as current context
            prevAnchorDegree,
            linkBpm.load(),
            currentBeatPosition,
            phrasePosition,
            loopDetected,
            anchorHistory);
        primarySuggestion.store (sug.primary);
        secondarySuggestion.store (sug.secondary);
        currentProgressionName = suggestionEngine.detectProgression (recentDegrees);
    }

    // Sync CCs
    out.addEvent (juce::MidiMessage::controllerEvent (1, 1, degree), samplePosition);
    out.addEvent (juce::MidiMessage::controllerEvent (1, 2, harmonyEngine.getRootMidi() - 48), samplePosition);
    int moodIdx = 0;
    for (int i = 0; i < HarmonyEngine::moodNames.size(); ++i)
        if (HarmonyEngine::moodNames[i] == harmonyEngine.getCurrentMood()) { moodIdx = i; break; }
    out.addEvent (juce::MidiMessage::controllerEvent (1, 3, moodIdx), samplePosition);
    out.addEvent (juce::MidiMessage::controllerEvent (1, 4, juce::roundToInt (colorAmount.load() * 127.0f)), samplePosition);
    out.addEvent (juce::MidiMessage::controllerEvent (1, 5, juce::roundToInt (feelAmount.load() * 127.0f)), samplePosition);
    out.addEvent (juce::MidiMessage::controllerEvent (1, 6, juce::roundToInt (driftAmount.load() * 127.0f)), samplePosition);

#if JUCE_DEBUG
    juce::String dbg;
    for (int nn : currentChordNotes) dbg += juce::String (nn) + " ";
    DBG ("Chord deg=" + juce::String (degree) + " [" + dbg.trimEnd() + "]"
         + " feel=" + juce::String (feel, 2) + " drift=" + juce::String (drift, 2));
#endif
}

// ── Release chord ──────────────────────────────────────────────────────────

void FormaProcessor::releaseChord (juce::MidiBuffer& out, int samplePosition)
{
    const bool relChords = chordsEnabled.load();
    const bool relBass   = bassEnabled.load();

    pendingNotes.clear();

    if (relChords)
        for (int note : currentChordNotes)
            out.addEvent (juce::MidiMessage::noteOff (kChordChannel, note), samplePosition);

    // ALWAYS release chord notes on chord synth (catch stuck voices).
    for (int note : currentChordNotes)
        chordSynthMidi.addEvent (juce::MidiMessage::noteOff (1, note), samplePosition);
    currentChordNotes.clear();

    // Release bass
    if (currentBassNote >= 0)
    {
        if (relBass)
            out.addEvent (juce::MidiMessage::noteOff (kBassChannel, currentBassNote), samplePosition);
        bassSynthMidi.addEvent (juce::MidiMessage::noteOff (1, currentBassNote), samplePosition);
        currentBassNote = -1;
    }
    triggeredBassPlaying = -1;
    bassEngine.releaseChord();

    // Safety: all-notes-off on both synths to catch leaked voices.
    chordSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), samplePosition);
    bassSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), samplePosition);

    // ── Anchor system: commit if held long enough ──
    if (lastPlayedDegree >= 1)
    {
        double now = juce::Time::getMillisecondCounterHiRes();
        double holdDuration = now - lastChordPressTimeMs;
        if (holdDuration >= anchorHoldThresholdMs)
            commitAnchor (lastPlayedDegree);
    }

    currentDegree = -1;
    activeDegree.store (-1);
}

// ── Editor-triggered chord ─────────────────────────────────────────────────

void FormaProcessor::triggerChordFromEditor (int degree, juce::uint8 velocity)
{
    static const int degreeToNote[] = { 0, 60, 62, 64, 65, 67, 69, 71 };
    if (degree < 1 || degree > 7) return;
    const juce::SpinLock::ScopedLockType lock (editorMidiLock);
    editorMidi.addEvent (juce::MidiMessage::noteOn (1, degreeToNote[degree], velocity), 0);
}

void FormaProcessor::releaseChordFromEditor()
{
    static const int degreeToNote[] = { 0, 60, 62, 64, 65, 67, 69, 71 };
    const juce::SpinLock::ScopedLockType lock (editorMidiLock);
    for (int d = 1; d <= 7; ++d)
        editorMidi.addEvent (juce::MidiMessage::noteOff (1, degreeToNote[d]), 0);
    // Clear held counts so MIDI-keyboard state stays consistent
    std::memset (heldDegreeCounts, 0, sizeof (heldDegreeCounts));
}

juce::AudioProcessorEditor* FormaProcessor::createEditor()
{ return new FormaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{ return new FormaProcessor(); }
