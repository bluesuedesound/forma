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

    // Initialize three synth engines
    chordSynth.addSound (new FormaSound());
    for (int i = 0; i < 8; ++i) chordSynth.addVoice (new FormaVoice());

    bassSynth.addSound (new FormaSound());
    for (int i = 0; i < 2; ++i) bassSynth.addVoice (new FormaVoice());

    arpSynth.addSound (new FormaSound());
    for (int i = 0; i < 6; ++i) arpSynth.addVoice (new FormaVoice());

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
        p.xyDotX = 0.5f; p.xyDotY = 0.25f;  // dotY inverted: high color = low dotY
        p.bassOn = true; p.bassAlt = true; p.bassOctave = -1;
        p.arpEnabled = false; p.arpMotif = 0; p.arpRate = 1.0f;
        p.arpGate = 0.8f; p.arpSpread = 1; p.arpOctave = 0;
        p.outputMode = 0; p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
    {
        auto& p = presets[1];
        p.name = "golden hour"; p.mood = "Bright"; p.keyRoot = 7; // G
        p.colorAmount = 0.5f; p.feelAmount = 0.3f;
        p.xyDotX = 0.3f; p.xyDotY = 0.5f;
        p.bassOn = true; p.bassAlt = false; p.bassOctave = -1;
        p.arpEnabled = true; p.arpMotif = 0; p.arpRate = 1.0f;
        p.arpGate = 0.7f; p.arpSpread = 1; p.arpOctave = 0;
        p.outputMode = 0; p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
    {
        auto& p = presets[2];
        p.name = "Dream Wash"; p.mood = "Dream"; p.keyRoot = 5; // F
        p.colorAmount = 0.7f; p.feelAmount = 0.6f;
        p.xyDotX = 0.6f; p.xyDotY = 0.3f;
        p.bassOn = true; p.bassAlt = false; p.bassOctave = -1;
        p.arpEnabled = true; p.arpMotif = 4; p.arpRate = 0.5f;
        p.arpGate = 0.85f; p.arpSpread = 2; p.arpOctave = 0;
        p.outputMode = 0; p.syncMode = 2; p.synthVolume = 0.7f;
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
        p.bassOn = true; p.bassAlt = false; p.bassOctave = -1;
        p.arpEnabled = false; p.arpMotif = 0; p.arpRate = 1.0f;
        p.arpGate = 0.8f; p.arpSpread = 1; p.arpOctave = 0;
        p.outputMode = 0; p.syncMode = 2; p.synthVolume = 0.7f;
        p.isEmpty = false;
    }
}

void FormaProcessor::applySoundPreset (int preset)
{
    using namespace FormaSynthPresets;

    FormaVoiceParams chordP, bassP, arpP;
    switch (preset)
    {
        case 0: chordP = Keys();  bassP = Sub();     arpP = Pluck();  break; // Keys
        case 1: chordP = Felt();  bassP = Rubber();  arpP = Pluck();  break; // Felt
        case 2: chordP = Glass(); bassP = Sub();     arpP = Bell();   break; // Glass
        case 3: chordP = Tape();  bassP = Vintage(); arpP = Pluck();  break; // Tape
        case 4: chordP = PadPreset(); bassP = Sub(); arpP = Bell();   break; // Ambient
        case 5: chordP = Keys();  bassP = Sub();     arpP = Mallet(); break; // Mallet
        default: chordP = Keys(); bassP = Sub();     arpP = Pluck();  break;
    }

    for (int i = 0; i < chordSynth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<FormaVoice*> (chordSynth.getVoice (i)))
            v->params = chordP;
    for (int i = 0; i < bassSynth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<FormaVoice*> (bassSynth.getVoice (i)))
            v->params = bassP;
    for (int i = 0; i < arpSynth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<FormaVoice*> (arpSynth.getVoice (i)))
            v->params = arpP;

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

    // Bass
    xml->setAttribute ("bassOn",  bassEnabledParam.load());
    xml->setAttribute ("bassAlt", bassAltParam.load());
    xml->setAttribute ("bassOct", octaveBassParam.load());

    // Arp
    xml->setAttribute ("arpEnabled", arpEnabled.load());
    xml->setAttribute ("arpMotif",   (int) arpeggiator.getPattern());
    xml->setAttribute ("arpRate",    (double) arpeggiator.getRate());
    xml->setAttribute ("arpGate",    (double) arpeggiator.getGate());
    xml->setAttribute ("arpSpread",  arpeggiator.getSpread());
    xml->setAttribute ("arpOctave",  arpeggiator.getOctaveOffset());

    // Output/Sync
    xml->setAttribute ("outputMode", outputMode.load());
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
            pe->setAttribute ("bassOn",  presets[i].bassOn);
            pe->setAttribute ("bassAlt", presets[i].bassAlt);
            pe->setAttribute ("bassOct", presets[i].bassOctave);
            pe->setAttribute ("arpOn",   presets[i].arpEnabled);
            pe->setAttribute ("motif",   presets[i].arpMotif);
            pe->setAttribute ("rate",    (double) presets[i].arpRate);
            pe->setAttribute ("gate",    (double) presets[i].arpGate);
            pe->setAttribute ("spread",  presets[i].arpSpread);
            pe->setAttribute ("arpOct",  presets[i].arpOctave);
            pe->setAttribute ("outMode", presets[i].outputMode);
            pe->setAttribute ("sync",    presets[i].syncMode);
            pe->setAttribute ("synth",   (double) presets[i].synthVolume);
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

    // Bass
    bassEnabledParam.store (xml->getBoolAttribute ("bassOn", true));
    bassAltParam.store     (xml->getBoolAttribute ("bassAlt", false));
    octaveBassParam.store  (xml->getIntAttribute  ("bassOct", -1));

    // Arp
    arpEnabled.store (xml->getBoolAttribute ("arpEnabled", false));
    arpeggiator.setPattern ((Arpeggiator::Pattern) xml->getIntAttribute ("arpMotif", 0));
    arpeggiator.setRate    ((float) xml->getDoubleAttribute ("arpRate", 1.0));
    arpeggiator.setGate    ((float) xml->getDoubleAttribute ("arpGate", 0.8));
    arpeggiator.setSpread  (xml->getIntAttribute ("arpSpread", 1));
    arpeggiator.setOctaveOffset (xml->getIntAttribute ("arpOctave", 0));
    arpRate.store      ((float) xml->getDoubleAttribute ("arpRate", 1.0));
    arpGateParam.store ((float) xml->getDoubleAttribute ("arpGate", 0.8));
    arpSpread.store    (xml->getIntAttribute ("arpSpread", 1));
    arpOctave.store    (xml->getIntAttribute ("arpOctave", 0));

    // Output/Sync
    outputMode.store (xml->getIntAttribute ("outputMode", 0));
    syncMode.store   (xml->getIntAttribute ("syncMode", 2));

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
            presets[slot].bassOn      = pe->getBoolAttribute ("bassOn");
            presets[slot].bassAlt     = pe->getBoolAttribute ("bassAlt");
            presets[slot].bassOctave  = pe->getIntAttribute ("bassOct");
            presets[slot].arpEnabled  = pe->getBoolAttribute ("arpOn");
            presets[slot].arpMotif    = pe->getIntAttribute ("motif");
            presets[slot].arpRate     = (float) pe->getDoubleAttribute ("rate");
            presets[slot].arpGate     = (float) pe->getDoubleAttribute ("gate");
            presets[slot].arpSpread   = pe->getIntAttribute ("spread");
            presets[slot].arpOctave   = pe->getIntAttribute ("arpOct");
            presets[slot].outputMode  = pe->getIntAttribute ("outMode");
            presets[slot].syncMode    = pe->getIntAttribute ("sync");
            presets[slot].synthVolume = (float) pe->getDoubleAttribute ("synth");
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
    p.name        = name;
    p.mood        = harmonyEngine.getCurrentMood();
    p.keyRoot     = harmonyEngine.getRootMidi() - 48;
    p.colorAmount = colorAmount.load();
    p.feelAmount  = feelAmount.load();
    p.xyDotX      = xyDotX.load();
    p.xyDotY      = xyDotY.load();
    p.bassOn      = bassEnabledParam.load();
    p.bassAlt     = bassAltParam.load();
    p.bassOctave  = octaveBassParam.load();
    p.arpEnabled  = arpEnabled.load();
    p.arpMotif    = (int) arpeggiator.getPattern();
    p.arpRate     = arpeggiator.getRate();
    p.arpGate     = arpeggiator.getGate();
    p.arpSpread   = arpeggiator.getSpread();
    p.arpOctave   = arpeggiator.getOctaveOffset();
    p.outputMode  = outputMode.load();
    p.syncMode    = syncMode.load();
    p.synthVolume = synthVolume.load();
    p.isEmpty     = false;
    currentPresetIndex = slot;
}

void FormaProcessor::loadPreset (int slot)
{
    if (slot < 0 || slot >= NUM_PRESETS) return;
    auto& p = presets[slot];
    if (p.isEmpty) return;

    // Release any active notes before changing state
    resetPlayingState();

    harmonyEngine.setMood (p.mood);
    harmonyEngine.setKey (48 + juce::jlimit (0, 11, p.keyRoot));
    colorAmount.store (p.colorAmount);
    feelAmount.store (p.feelAmount);
    harmonyEngine.setColorAmount (p.colorAmount);
    xyDotX.store (p.xyDotX);
    xyDotY.store (p.xyDotY);
    bassEnabledParam.store (p.bassOn);
    bassAltParam.store (p.bassAlt);
    octaveBassParam.store (p.bassOctave);
    arpEnabled.store (p.arpEnabled);
    arpeggiator.setPattern ((Arpeggiator::Pattern) p.arpMotif);
    arpeggiator.setRate (p.arpRate);
    arpeggiator.setGate (p.arpGate);
    arpeggiator.setSpread (p.arpSpread);
    arpeggiator.setOctaveOffset (p.arpOctave);
    arpRate.store (p.arpRate);
    arpGateParam.store (p.arpGate);
    arpSpread.store (p.arpSpread);
    arpOctave.store (p.arpOctave);
    outputMode.store (p.outputMode);
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
    Arpeggiator::Pattern pattern;
    float rate, gate, feel, drift, color;
    int spread, voicing;
    bool bassAlt;
};

static const MoodDef kMoodDefs[] = {
    // Bright:  Rise,    rate 1x,   gate 0.70
    { Arpeggiator::Pattern::Rise,    1.0f,  0.70f, 0.30f, 0.15f, 0.50f, 1,  0, false },
    // Warm:    Groove,  rate 1x,   gate 0.75
    { Arpeggiator::Pattern::Groove,  1.0f,  0.75f, 0.40f, 0.35f, 0.60f, 1, -1, true  },
    // Dream:   Spiral,  rate 0.5x, gate 0.85
    { Arpeggiator::Pattern::Spiral,  0.5f,  0.85f, 0.60f, 0.25f, 0.70f, 2,  3, false },
    // Deep:    Pulse,   rate 1x,   gate 0.70
    { Arpeggiator::Pattern::Pulse,   1.0f,  0.70f, 0.50f, 0.45f, 0.75f, 1, -1, true  },
    // Hollow:  Cascade, rate 0.5x, gate 0.90
    { Arpeggiator::Pattern::Cascade, 0.5f,  0.90f, 0.15f, 0.10f, 0.40f, 1,  1, false },
    // Tender:  Pulse,   rate 0.5x, gate 0.85
    { Arpeggiator::Pattern::Pulse,   0.5f,  0.85f, 0.60f, 0.35f, 0.70f, 1,  1, false },
    // Tense:   Cascade, rate 2x,   gate 0.65
    { Arpeggiator::Pattern::Cascade, 2.0f,  0.65f, 0.35f, 0.20f, 0.55f, 1, -2, true  },
    // Dusk:    Groove,  rate 1x,   gate 0.75
    { Arpeggiator::Pattern::Groove,  1.0f,  0.75f, 0.50f, 0.40f, 0.55f, 1,  0, false },
    // ── Bright Lights pack ──
    // Crest:   Rise,    rate 1x,   gate 0.70 — clean pop
    { Arpeggiator::Pattern::Rise,    1.0f,  0.70f, 0.25f, 0.10f, 0.45f, 1,  0, false },
    // Nocturne: Pulse,  rate 0.5x, gate 0.80 — dark minor
    { Arpeggiator::Pattern::Pulse,   0.5f,  0.80f, 0.40f, 0.30f, 0.35f, 1, -1, false },
    // Shimmer: Spiral,  rate 1x,   gate 0.75 — synth
    { Arpeggiator::Pattern::Spiral,  1.0f,  0.75f, 0.35f, 0.20f, 0.50f, 1,  1, false },
    // Static:  Cascade, rate 2x,   gate 0.65 — hyperpop
    { Arpeggiator::Pattern::Cascade, 2.0f,  0.65f, 0.20f, 0.15f, 0.50f, 1,  0, false },
};

void FormaProcessor::applyMoodDefaults (int moodIndex)
{
    if (moodIndex < 0 || moodIndex >= 12) return;
    const auto& d = kMoodDefs[moodIndex];

    arpeggiator.setPattern (d.pattern);
    arpRate.store (d.rate);
    arpGateParam.store (d.gate);
    arpSpread.store (d.spread);

    feelAmount.store (d.feel);
    driftAmount.store (d.drift);
    colorAmount.store (d.color);
    harmonyEngine.setColorAmount (d.color);

    voicingParam.store (d.voicing);
    bassAltParam.store (d.bassAlt);

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

    chordSynth.setCurrentPlaybackSampleRate (sampleRate);
    bassSynth.setCurrentPlaybackSampleRate (sampleRate);
    arpSynth.setCurrentPlaybackSampleRate (sampleRate);

    for (int i = 0; i < chordSynth.getNumVoices(); ++i)
        static_cast<FormaVoice*> (chordSynth.getVoice (i))->prepareToPlay (sampleRate, 0);
    for (int i = 0; i < bassSynth.getNumVoices(); ++i)
        static_cast<FormaVoice*> (bassSynth.getVoice (i))->prepareToPlay (sampleRate, 0);
    for (int i = 0; i < arpSynth.getNumVoices(); ++i)
        static_cast<FormaVoice*> (arpSynth.getVoice (i))->prepareToPlay (sampleRate, 0);
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
        editorMidi.addEvent (juce::MidiMessage::allNotesOff (kArpChannel), 0);
        // Also send note-offs for all known active notes
        for (int note : currentChordNotes)
        {
            editorMidi.addEvent (juce::MidiMessage::noteOff (kChordChannel, note), 0);
            editorMidi.addEvent (juce::MidiMessage::noteOff (1, note), 0);  // synth channel
        }
        if (currentBassNote >= 0)
        {
            editorMidi.addEvent (juce::MidiMessage::noteOff (kBassChannel, currentBassNote), 0);
            editorMidi.addEvent (juce::MidiMessage::noteOff (1, currentBassNote), 0);
        }
    }

    pendingNotes.clear();
    arpeggiator.setActive (false);
    arpeggiator.reset();
    currentChordNotes.clear();
    currentBassNote = -1;
    currentDegree = -1;
    activeDegree.store (-1);
    std::memset (heldDegreeCounts, 0, sizeof (heldDegreeCounts));
    chordSynth.allNotesOff (0, true);
    bassSynth.allNotesOff (0, true);
    arpSynth.allNotesOff (0, true);
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
    bassEnabled = bassEnabledParam.load();
    bassAlt     = bassAltParam.load();
    harmonyEngine.setColorAmount (colorAmount.load());
    harmonyEngine.setVoicing (voicingParam.load());

    // Sync arp params (don't touch active state here — controlled by
    // triggerChord/releaseChord based on whether a chord is held)
    arpeggiator.setRate (arpRate.load());
    arpeggiator.setGate (arpGateParam.load());
    arpeggiator.setSpread (arpSpread.load());
    arpeggiator.setOctaveOffset (arpOctave.load() * 12);
    arpeggiator.setFeelAmount (feelAmount.load());
    arpeggiator.setDriftAmount (driftAmount.load());

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

    // Detect output mode change — send all-notes-off on all channels to prevent stuck notes
    int curMode = outputMode.load();
    if (curMode != prevOutputMode)
    {
        midiMessages.addEvent (juce::MidiMessage::allNotesOff (kChordChannel), 0);
        midiMessages.addEvent (juce::MidiMessage::allNotesOff (kBassChannel), 0);
        midiMessages.addEvent (juce::MidiMessage::allNotesOff (kArpChannel), 0);
        chordSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        bassSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        arpSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        chordSynth.allNotesOff (0, false);
        bassSynth.allNotesOff (0, false);
        arpSynth.allNotesOff (0, false);
        prevOutputMode = curMode;
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
    arpSynthMidi.clear();

    // Kill arp if toggle was turned off
    if (! arpEnabled.load() && arpeggiator.isActive())
    {
        auto mode = static_cast<OutputMode> (outputMode.load());
        int arpOff = arpeggiator.flushPendingOff();
        arpeggiator.setActive (false);
        arpeggiator.reset();
        if (arpOff >= 0)
        {
            bool sendArp = (mode == OutputMode::All || mode == OutputMode::Arp);
            if (sendArp)
            {
                int ch = (mode == OutputMode::All) ? kArpChannel : 1;
                output.addEvent (juce::MidiMessage::noteOff (ch, arpOff), 0);
                if (mode == OutputMode::Arp)
                    arpSynthMidi.addEvent (juce::MidiMessage::noteOff (1, arpOff), 0);
            }
        }
    }

    processPendingNotes (output, currentBlockSize);

    // Safety: cap pending notes to prevent unbounded growth
    if (pendingNotes.size() > 32)
        pendingNotes.clear();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        int pos  = metadata.samplePosition;

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

    // ── Arpeggiator ──
    auto mode = static_cast<OutputMode> (outputMode.load());
    bool sendArp = (mode == OutputMode::All || mode == OutputMode::Arp);

    if (sendArp && arpeggiator.isActive())
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
        {
            auto posInfo = ph->getPosition();
            if (posInfo.hasValue())
                if (auto b = posInfo->getBpm()) bpm = *b;
        }

        int ch = (mode == OutputMode::All) ? kArpChannel : 1;
        auto events = arpeggiator.process (currentBlockSize, bpm, currentSampleRate);

        for (auto& e : events)
        {
            if (e.isNoteOn)
            {
                output.addEvent (juce::MidiMessage::noteOn (ch, e.note, (juce::uint8) e.velocity), e.sampleOffset);
                arpSynthMidi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.velocity), e.sampleOffset);
                lastArpNote.store (e.note);
            }
            else
            {
                output.addEvent (juce::MidiMessage::noteOff (ch, e.note), e.sampleOffset);
                arpSynthMidi.addEvent (juce::MidiMessage::noteOff (1, e.note), e.sampleOffset);
            }
        }
    }

    // Render three synth engines — they sum into the buffer
    chordSynth.renderNextBlock (buffer, chordSynthMidi, 0, currentBlockSize);
    bassSynth.renderNextBlock  (buffer, bassSynthMidi,  0, currentBlockSize);
    arpSynth.renderNextBlock   (buffer, arpSynthMidi,   0, currentBlockSize);

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

    auto mode = static_cast<OutputMode> (outputMode.load());
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

    // Voice leading: melodic nearest-note
    harmonyEngine.setFeelAmount (feel);

    std::vector<int> voiced;
    if (feel < 0.15f || prevChordNotes.empty())
    {
        // Below feel threshold or first chord: root position near mood register
        voiced = harmonyEngine.placeNearRegister (rawChord,
                     harmonyEngine.getTargetRegisterCenter());
    }
    else
    {
        voiced = harmonyEngine.getBestInversion (rawChord, prevChordNotes, feel, degree);
    }

    // Store full 4-voice result for voice leading context
    for (auto& n : voiced) n = juce::jlimit (21, 108, n);
    prevChordNotes = voiced;  // always 4 voices for voice leading

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
    bool sendChords = (mode == OutputMode::All || mode == OutputMode::Chords);
    int chordCh = (mode == OutputMode::All) ? kChordChannel : 1;
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

    // Bass — simple toggle: root note one octave below chord
    if (bassEnabled)
    {
        int rootPc = rawChord[0] % 12;
        int lowestVoice = currentChordNotes.empty() ? 48 : currentChordNotes.front();
        int bassNote = lowestVoice - 12;
        while (bassNote % 12 != rootPc) bassNote--;
        if (bassNote < 24) bassNote += 12;
        bassNote = juce::jlimit (24, 60, bassNote);
        bassNote = juce::jlimit (21, 72, bassNote + octaveBass * 12);
        currentBassNote = bassNote;

        bool sendBass = (mode == OutputMode::All || mode == OutputMode::Bass);
        if (sendBass)
        {
            int bassCh = (mode == OutputMode::All) ? kBassChannel : 1;
            int bassOffset = juce::jmin (samplePosition, currentBlockSize - 1);
            out.addEvent (juce::MidiMessage::noteOn (bassCh, currentBassNote, (juce::uint8) 70), bassOffset);
            bassSynthMidi.addEvent (juce::MidiMessage::noteOn (1, currentBassNote, (juce::uint8) 70), bassOffset);
        }
    }

    // Feed chord to arpeggiator — restart from position 0
    auto sorted = currentChordNotes;
    std::sort (sorted.begin(), sorted.end());
    arpeggiator.setChord (sorted);
    arpeggiator.reset();
    if (arpEnabled.load())
        arpeggiator.setActive (true);

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

    // Update register drift tracking
    harmonyEngine.updateDriftTracking (currentChordNotes);

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
    auto mode = static_cast<OutputMode> (outputMode.load());

    // Cancel ALL pending strum note-ons
    pendingNotes.clear();

    // Release chord notes on MIDI output
    bool relChords = (mode == OutputMode::All || mode == OutputMode::Chords);
    if (relChords)
    {
        int ch = (mode == OutputMode::All) ? kChordChannel : 1;
        for (int note : currentChordNotes)
            out.addEvent (juce::MidiMessage::noteOff (ch, note), samplePosition);
    }

    // ALWAYS release chord notes on chord synth (regardless of output mode)
    for (int note : currentChordNotes)
        chordSynthMidi.addEvent (juce::MidiMessage::noteOff (1, note), samplePosition);
    currentChordNotes.clear();

    // Release bass
    if (currentBassNote >= 0)
    {
        bool relBass = (mode == OutputMode::All || mode == OutputMode::Bass);
        if (relBass)
        {
            int ch = (mode == OutputMode::All) ? kBassChannel : 1;
            out.addEvent (juce::MidiMessage::noteOff (ch, currentBassNote), samplePosition);
        }
        bassSynthMidi.addEvent (juce::MidiMessage::noteOff (1, currentBassNote), samplePosition);
        currentBassNote = -1;
    }

    // Stop arp
    arpeggiator.setActive (false);
    int arpOff = arpeggiator.flushPendingOff();
    if (arpOff >= 0)
    {
        bool relArp = (mode == OutputMode::All || mode == OutputMode::Arp);
        if (relArp)
        {
            int ch = (mode == OutputMode::All) ? kArpChannel : 1;
            out.addEvent (juce::MidiMessage::noteOff (ch, arpOff), samplePosition);
        }
        arpSynthMidi.addEvent (juce::MidiMessage::noteOff (1, arpOff), samplePosition);
    }
    arpeggiator.reset();

    // Safety: all-notes-off on all synths to catch leaked voices
    chordSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), samplePosition);
    bassSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), samplePosition);
    arpSynthMidi.addEvent (juce::MidiMessage::allNotesOff (1), samplePosition);

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
