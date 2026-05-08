#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "HarmonyEngine.h"
#include "FormaSynth.h"
#include "BassEngine.h"
#include "SuggestionEngine.h"

struct FormaPreset {
    juce::String name;
    juce::String mood     = "Bright";
    int   keyRoot         = 0;       // 0-11 (pitch class, NOT midi note)
    float colorAmount     = 0.5f;
    float feelAmount      = 0.0f;
    float xyDotX          = 0.5f;
    float xyDotY          = 0.5f;
    int   bassOctave      = -1;
    int   bassMode        = 0;       // 0=Root,1=KickTrigger,2=KickVariation
    int   bassTriggerNote = 0;       // MIDI note
    float bassVariation   = 0.30f;
    bool  chordsEnabled   = true;
    bool  bassEnabled     = true;
    int   syncMode        = 2;
    float synthVolume     = 0.7f;
    bool  isEmpty         = true;
};

class FormaProcessor : public juce::AudioProcessor
{
public:
    enum class SoundPreset { Keys = 0, Felt, Glass, Tape, Ambient, Mallet };
    std::atomic<int> currentSoundPreset { 0 };
    void applySoundPreset (int preset);

    enum class VoicingMode { Full = 0, Upper = 1, Shell = 2 };
    std::atomic<int> voicingMode { 0 };

    enum class SyncMode { Full = 0, Expressive = 1, Harmonic = 2, Free = 3 };
    std::atomic<int> syncMode { 2 };  // default: Harmonic

    FormaProcessor();
    ~FormaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    HarmonyEngine harmonyEngine;

    // ── Thread-safe parameters ──────────────────────────────────────────
    std::atomic<float> synthVolume      { 0.7f };
    std::atomic<float> colorAmount      { 0.0f };
    std::atomic<float> feelAmount       { 0.0f };
    std::atomic<float> driftAmount      { 0.0f };
    std::atomic<int>   octaveChordParam { 0 };
    std::atomic<int>   octaveBassParam  { -1 };
    std::atomic<int>   voicingParam     { 0 };

    // ── Voice enable toggles (right column) ─────────────────────────────
    std::atomic<bool>  chordsEnabled { true };
    std::atomic<bool>  bassEnabled   { true };

    // ── Bass mode parameters ────────────────────────────────────────────
    // 0=Root, 1=KickTrigger, 2=KickVariation
    std::atomic<int>   bassMode              { 0 };
    std::atomic<int>   bassTriggerNoteParam  { 0 };  // MIDI note; default C-2 (Ableton)
    std::atomic<float> bassVariationAmount   { 0.30f };

    void triggerChordFromEditor (int degree, juce::uint8 velocity = 80);
    void releaseChordFromEditor();
    void applyMoodDefaults (int moodIndex);

    // ── Sketch / Compose mode ─────────────────────────────────────────────
    // 0 = Sketch (live performance), 1 = Compose (locked progression
    // played back by host transport). Default Sketch.
    std::atomic<int> composeMode { 0 };

    struct ComposeStep {
        std::atomic<int> degree       { 0 };  // 0=off, 1..7
        std::atomic<int> duration     { 4 };  // quarter beats: 1,2,3,4,6,8,12,16
        std::atomic<int> articulation { 0 };  // 0=Sustain, 1=Stab, 2=Pulse
        std::atomic<int> rhythmPattern{ 0 };  // 0=None, 1=Beats, 2=1and3,
                                              // 3=Offbeats, 4=Eighths, 5=Sixteenths
    };
    static constexpr int kComposeSteps = 16;
    std::array<ComposeStep, kComposeSteps> composeSteps;

    // Currently-playing step indicator for the editor (display only).
    std::atomic<int> composeDisplayStep { -1 };

    // Mood snapshot saved at capture time. Persisted alongside compose
    // steps so a session restored from disk recalls the captured mood.
    std::atomic<float> snapshotColor    { 0.5f };
    std::atomic<float> snapshotFeel     { 0.0f };
    std::atomic<int>   snapshotKey      { 0 };
    std::atomic<int>   snapshotChordOct { 0 };
    std::atomic<int>   snapshotMoodIdx  { 0 };
    std::atomic<bool>  snapshotValid    { false };

    // Recent chord presses, used by Capture to estimate per-step durations.
    struct ChordPress {
        int    degree      = 0;
        double timeInBeats = 0.0;
    };
    std::array<ChordPress, kComposeSteps> recentPresses;
    int            pressCount    = 0;
    int            nextPressSlot = 0;
    juce::SpinLock recentPressLock;

    // Capture: read the most recent chord history, fill composeSteps,
    // save mood snapshot, switch mode to Compose. Returns count written.
    int captureFromSketch();

    std::atomic<int> activeDegree { -1 };
    std::atomic<float> lastChordVelocity { 0.8f };
    juce::String currentChordName;

    // ── Suggestion system ───────────────────────────────────────────────
    SuggestionEngine suggestionEngine;
    std::atomic<int> primarySuggestion   { -1 };
    std::atomic<int> secondarySuggestion { -1 };
    juce::String currentProgressionName;
    std::atomic<double> linkBpm { 120.0 };
    std::atomic<bool>   linkActive { false };

    // XY dot position (normalized 0..1) for editor state persistence
    std::atomic<float> xyDotX { 0.5f };
    std::atomic<float> xyDotY { 0.5f };

    // Suggestion visibility toggle
    std::atomic<bool> suggestionsVisible { true };
    void manualHarmonicReset();

    // Drift — mood-informed, not user-controlled
    float getDriftForMoodAndFeel (float feelAmount);

    // ── Preset system ───────────────────────────────────────────────────
    static const int NUM_PRESETS = 8;
    FormaPreset presets[NUM_PRESETS];
    int currentPresetIndex = -1;  // -1 = unsaved state

    void savePreset (int slot, const juce::String& name);
    void loadPreset (int slot);

    BassEngine  bassEngine;

private:
    static constexpr int kChordChannel = 1;
    static constexpr int kBassChannel  = 2;

    static int pitchClassToDegree (int pitchClass);

    int currentDegree = -1;
    std::vector<int> currentChordNotes;
    int currentBassNote = -1;
    int triggeredBassPlaying = -1;  // pitch of currently-sounding trigger-fired bass, or -1
    std::vector<int> prevChordNotes;
    int heldDegreeCounts[8] = {};

    int octaveChord = 0;
    int octaveBass  = -1;

    juce::Synthesiser chordSynth;
    juce::Synthesiser bassSynth;
    juce::MidiBuffer chordSynthMidi;
    juce::MidiBuffer bassSynthMidi;

    juce::MidiBuffer editorMidi;
    juce::SpinLock   editorMidiLock;

    double currentSampleRate = 44100.0;

    // ── Feel/Drift: pending note queue ──────────────────────────────────
    struct PendingNote {
        int note, velocity, channel, samplesRemaining;
        bool isNoteOff;
    };
    std::vector<PendingNote> pendingNotes;
    juce::Random rng;

    // ── Drift accumulation per degree ───────────────────────────────────
    float driftAccum[8] = {};
    int   lastDriftDegree = -1;

    // ── Suggestion state ────────────────────────────────────────────────
    int lastPlayedDegree  = -1;
    int anchorDegree      = -1;
    int prevAnchorDegree  = -1;

    double lastChordPressTimeMs   = 0.0;
    double lastChordReleaseTimeMs = 0.0;
    double anchorHoldThresholdMs  = 250.0;

    std::vector<int> anchorHistory;   // max size 6
    std::vector<int> recentDegrees;   // for progression detection

    int  phrasePosition = 0;
    bool loopDetected   = false;

    // Key suggestion
    int nonTonicChordCount = 0;
    void checkKeySuggestion();

public:
    bool keySuggestionActive = false;
    juce::String keySuggestion;
private:

    void commitAnchor (int degree);
    void detectLoop();
    void recomputeSuggestions();
    void resetHarmonicState();

    int currentBlockSize = 512;
    // Snapshot of chordsEnabled/bassEnabled from the previous processBlock,
    // for detecting transitions and flushing stuck notes on voice-off.
    bool prevChordsEnabled = true;
    bool prevBassEnabled   = true;

    double lastKnownPpqPosition = 0.0;
    bool   transportWasPlaying  = false;
    float  currentBeatPosition  = 0.0f;

    // Compose playback state (audio thread only).
    int  composeCurrentStep      = -1;   // step index currently held
    int  composeActiveDegree     = -1;   // degree currently sounding via Compose
    int  composeCurrentEventIdx  = -1;   // sub-step rhythm event index (within current step)
    double composeReleaseDueBeat = 0.0;  // step-relative beat at which to release chord
    void updateComposePlayback (juce::MidiBuffer& out, int samplePosition,
                                 bool isPlaying, double ppq);
    void releaseComposeChord  (juce::MidiBuffer& out, int samplePosition);
    int  composeStepAtBeat    (double beats, double& stepStartOut) const;

    void triggerChord (int degree, juce::uint8 velocity, juce::MidiBuffer& out,
                       int samplePosition);
    void releaseChord (juce::MidiBuffer& out, int samplePosition);
    void processPendingNotes (juce::MidiBuffer& out, int blockSize);
    void resetPlayingState();  // clean shutdown: kill all notes, clear queues

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FormaProcessor)
};
