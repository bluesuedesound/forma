#pragma once
#include "PluginProcessor.h"

namespace C {
    const juce::Colour BG        (0xFF1A1814);
    const juce::Colour BG2       (0xFF141210);
    const juce::Colour BG3       (0xFF1E1C18);
    const juce::Colour BG4       (0xFF0F0E0B);
    const juce::Colour BORDER    (0xFF252318);
    const juce::Colour BORDER2   (0xFF2A2820);
    const juce::Colour DARK      (0xFF262420);
    const juce::Colour ACCENT    (0xFFC8875A);
    const juce::Colour ACCENT_DK (0xFF5A3A20);
    const juce::Colour TXT_HI    (0xFFE8E4DC);
    const juce::Colour TXT_MID   (0xFF9A9590);
    const juce::Colour TXT_DIM   (0xFF4A4840);
    const juce::Colour TXT_GHOST (0xFF3A3830);
    const juce::Colour TXT_DARK  (0xFF2A2820);
    const juce::Colour SUGGEST1  (0xFFC8875A);
    const juce::Colour SUGGEST2  (0xFF6A8FAA);
}

struct MoodInfo {
    const char* name;
    const char* desc;
    juce::uint32 bgColor;
};
static constexpr int kNumMoods = 12;
static const MoodInfo kMoods[] = {
    { "Bright",   "open \xc2\xb7 resolved",     0xFF1C1A12 },
    { "Warm",     "groove \xc2\xb7 golden",      0xFF1C1810 },
    { "Dream",    "floating \xc2\xb7 cinematic", 0xFF141418 },
    { "Deep",     "rich \xc2\xb7 soulful",       0xFF161412 },
    { "Hollow",   "sparse \xc2\xb7 melancholy",  0xFF121418 },
    { "Tender",   "intimate \xc2\xb7 nocturnal",  0xFF1A1416 },
    { "Tense",    "dramatic \xc2\xb7 urgent",    0xFF141416 },
    { "Dusk",     "warm \xc2\xb7 unresolved",    0xFF1A1610 },
    // ── Bright Lights pack ──
    { "Crest",    "airy \xc2\xb7 pop",           0xFF141820 },
    { "Nocturne", "dark \xc2\xb7 minor",         0xFF121218 },
    { "Shimmer",  "silver \xc2\xb7 synth",       0xFF141618 },
    { "Static",   "hot \xc2\xb7 hyperpop",       0xFF1A1218 },
};

enum ChordQuality { Major, Minor, Diminished, Augmented };

class FormaEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit FormaEditor (FormaProcessor&);
    ~FormaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void syncUIFromProcessor();

private:
    FormaProcessor& proc;

    // Mood
    int moodIdx = 0;

    // Key
    int keyIdx = 0;
    static const juce::StringArray keyNames;

    // Chord state
    struct ChordBtn {
        juce::String roman, name, qualLabel;
        ChordQuality qual = Major;
        bool pressed = false;
        bool hover = false;
        bool sug1 = false, sug2 = false;
    };
    ChordBtn chords[7];
    int pressedChordIdx = -1;

    // XY pad
    float dotX = 0.5f, dotY = 0.5f;
    float glowX = 0.5f, glowY = 0.5f;
    bool draggingDot = false;

    // Advanced overlay state
    bool advancedVisible = false;
    int  bassOct       = -1;
    float synthVol     = 0.7f;
    int   currentSyncMode = 2;

    // Suggestion pulse
    float sugPulse = 0.0f, sugDir = 1.0f;
    int lastDegree = -1;

    // BPM / Link
    double currentBpm = 120.0;
    bool   isLinkActive = false;

    juce::String statusChord { "Ready" };

    // Progression name timeout
    float progNameAge = 0.0f;

    // ── Thermal field visual ──
    int   lastKnownDeg = -1;
    float breathePhase = 0.0f;

    // Wave phase accumulators
    float voidPhases[4] = {};
    float voidPVels[4]  = {};
    float ringPhases[4] = {};
    float ringPVels[4]  = {};
    int   currentThermalIdx = 0;

    void drawThermalCircle (juce::Graphics& g, juce::Point<float> centre, float radius);
    void drawThermalDot (juce::Graphics& g, float dpx, float dpy);
    float voidRadius (float angle, float dotNX, float dotNY, float circleR);
    void updateWaveVelocities (int moodIdx);

    // Mood transition — bg color lerp
    juce::Colour currentBgColor;
    juce::Colour targetBgColor;
    float bgTransProgress = 1.0f;

    // Mood XY dot transition
    float targetDotX = 0.5f, targetDotY = 0.5f;
    bool  moodTransitioning = false;
    float moodTransProgress = 1.0f;

    // Layout rects
    juce::Rectangle<int> leftCol, centerCol, rightCol;
    juce::Rectangle<int> xyPadCircle;
    juce::Rectangle<int> compassContainer;
    juce::Rectangle<int> chordKeyRects[7];

    // Gear button rect
    juce::Rectangle<int> gearBtnRect;
    // Link indicator rect
    juce::Rectangle<int> linkRect;

    // Advanced overlay rects
    juce::Rectangle<int> advCloseRect;
    juce::Rectangle<int> advSoundPills[6];
    int currentSoundPreset = 0;
    juce::Rectangle<int> advVoicingPills[3];
    int currentVoicingMode = 0;
    juce::Rectangle<int> advSyncPills[4];
    juce::Rectangle<int> advChordOctMinRect, advChordOctPlRect;
    int chordOctVal = 0;
    juce::Rectangle<int> advSynthSlider;

    // Preset slot rects
    juce::Rectangle<int> presetSlotRects[8];

    // Suggestions toggle + reset
    juce::Rectangle<int> advSuggestionsToggleRect;
    juce::Rectangle<int> advSuggestionsResetRect;
    bool suggestionsOn = true;
    float resetFlashTimer = 0.0f;

    // ── Right column — voice toggles + bass section ──
    juce::Rectangle<int> rightChordsToggleRect;
    juce::Rectangle<int> rightBassToggleRect;
    juce::Rectangle<int> bassModePills[3];           // ROOT / KICK / KICK+VAR
    juce::Rectangle<int> bassOctMinRect, bassOctPlRect;
    juce::Rectangle<int> bassTrigNoteMinRect, bassTrigNotePlRect;
    juce::Rectangle<int> bassVariationSlider;
    bool  chordsEnabledUI = true;
    bool  bassEnabledUI   = true;
    int   bassModeUI      = 0;
    int   bassTrigNoteUI  = 0;
    float bassVariationUI = 0.30f;

    // Font helper
    juce::Font mono (float h) const;
    juce::Font sans (float h, bool bold = false) const;

    // Drawing
    void drawTopBar      (juce::Graphics& g);
    void drawLeftCol     (juce::Graphics& g);
    void drawCenter      (juce::Graphics& g);
    void drawRightCol    (juce::Graphics& g);
    void drawStatusBar   (juce::Graphics& g);
    void drawChordKey    (juce::Graphics& g, juce::Rectangle<int> r, int idx);
    void drawXYPad       (juce::Graphics& g);
    void drawAdvanced    (juce::Graphics& g);
    void drawPill        (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool active);
    void drawStepper     (juce::Graphics& g, juce::Rectangle<int> r, const char* label, int val);
    void drawToggle      (juce::Graphics& g, juce::Rectangle<int> r, bool on);
    void drawMiniSlider  (juce::Graphics& g, juce::Rectangle<int> r, float val);

    // Lofi grain texture — generated once in the constructor and tiled as a
    // low-opacity overlay over surfaces.
    juce::Image grainTile;
    void buildGrainTile();
    void drawGrainOverlay (juce::Graphics& g, juce::Rectangle<int> area, float opacity);

    // ── Per-pill animation state ──
    // Resting: no active chord; Active: chord held / persistent glow;
    // Flashing: 1100 ms chord-name-reveal animation overlay on top of Active.
    struct PillAnim {
        enum class State { Resting, Active, Flashing };
        State state = State::Resting;
        double flashStartMs = 0.0;
    };
    PillAnim pillAnim[7];
    bool anyPillAnimating() const;

    void updateChordLabels();
    void setMood (int idx);
    void setKey  (int idx);

    void timerCallback() override;

    // Mood hover
    int hoveredMoodIdx = -1;
    int hoveredChordIdx = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormaEditor)
};
