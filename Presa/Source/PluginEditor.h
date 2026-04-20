#pragma once
#include "PluginProcessor.h"
#include "FormaPalette.h"
#include "FormaLookAndFeel.h"
#include "WaveformDisplay.h"
#include "StateDotField.h"
#include "PadGrid.h"

class PresaEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit PresaEditor (PresaProcessor&);
    ~PresaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    PresaProcessor& proc;
    FormaLookAndFeel lnf;

    // ── Child components ───────────────────────────────────────────────────
    WaveformDisplay waveformDisplay;
    StateDotField   stateDotField;
    PadGrid         padGrid;

    // ── Capture bar buttons ────────────────────────────────────────────────
    juce::TextButton armBtn    { "ARM" };
    juce::TextButton captureBtn { "CAPTURE" };
    juce::TextButton stopBtn   { "STOP" };
    juce::TextButton modeBtn   { "SLICE" };   // SLICE ↔ SAMPLE toggle
    juce::TextButton autoSliceBtn { "AUTO-SLICE" };

    // ── SAMPLE-mode control panel ─────────────────────────────────────────
    juce::TextButton loopBtn    { "LOOP" };
    juce::TextButton reverseBtn { "REVERSE" };

    juce::Rectangle<int> pitchDragRect;
    juce::Rectangle<int> speedDragRect;

    // Drag state for the pitch/speed readouts
    enum class SampleDragTarget { None, Pitch, Speed };
    SampleDragTarget sampleDrag = SampleDragTarget::None;
    int   sampleDragStartY = 0;
    int   pitchAtDragStart = 0;
    float speedAtDragStart = 1.0f;

    // Apply the Forma pill styling that matches LOOP/REVERSE active state.
    void refreshSampleControlButtons();

    // Redraw the SAMPLE control panel (invoked from paint()).
    void drawSampleControlPanel (juce::Graphics& g, juce::Rectangle<int> bounds);

    // Snap a live speed value to the discrete UI set.
    static float snapSpeed (float speed);
    // Index (and count) for the discrete speed set.
    static int   speedIndex (float speed);
    static float speedFromIndex (int idx);

   #if JucePlugin_Build_Standalone
    juce::TextButton exportBtn { "EXPORT" };
    void exportSample();
    void flashExportButton (bool success);

    // Time (in the Time::getMillisecondCounter() domain) at which the
    // flash colour should revert to the default idle style. 0 = not flashing.
    juce::uint32 exportFlashUntilMs = 0;
    bool         exportFlashSuccess = false;
   #endif

    // Keep the mode button's label in sync with processor state.
    void refreshModeButton();

    // Root note
    int rootNoteDisplay = 60;  // C4
    juce::Rectangle<int> rootNoteRect;
    juce::Rectangle<int> rootNoteUpRect;
    juce::Rectangle<int> rootNoteDnRect;

    // ── Layout constants ───────────────────────────────────────────────────
    static constexpr int kWidth       = 780;
    static constexpr int kHeight      = 564;
    static constexpr int kTopBarH     = 40;
    static constexpr int kWaveformH   = 180;
    static constexpr int kCaptureBarH = 40;
    static constexpr int kStatusBarH  = 24;

    // ── Drawing ────────────────────────────────────────────────────────────
    void drawTopBar    (juce::Graphics& g);
    void drawCaptureBar (juce::Graphics& g);
    void drawStatusBar (juce::Graphics& g);

    // ── Font helpers ───────────────────────────────────────────────────────
    juce::Font mono (float h) const;

    void timerCallback() override;

    // Capture state dot colour (animated)
    juce::Colour getCaptureStateColour() const;
    float captureDotPhase = 0.0f;

    // Note name helper
    static juce::String noteName (int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresaEditor)
};
