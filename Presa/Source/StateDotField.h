#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "FormaPalette.h"

// Forward declaration
struct PadState;

class StateDotField : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kNumPads = 16;

    StateDotField();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // Link to processor pad data (called once after construction)
    void setPadStates (PadState* padArray, int numPads);

    // Set which pad is selected (brings its dot into focus)
    void setSelectedPad (int padIndex);
    int  getSelectedPad() const { return selectedPadIdx; }

    // Callback when the user drags the selected pad's dot
    std::function<void (int padIndex, float x, float y)> onDotMoved;

    // Swap axis labels (SLICE / SAMPLE mode carry different semantics).
    void setAxisLabels (juce::String left, juce::String right,
                        juce::String bottom, juce::String top);

    // When true, the selected-dot glow gains an extra outer ring to
    // signal that the AmbientEngine is actively producing feedback.
    void setAmbientEngaged (bool engaged);

private:
    void timerCallback() override;

    // Draw the thermal gradient background based on dot position
    void drawThermalGradient (juce::Graphics& g, juce::Rectangle<float> bounds,
                              float dotX, float dotY);
    void drawAxisLabels (juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawGhostDots  (juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawSelectedDot (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Glow ring (3-pass)
    void drawGlowRing (juce::Graphics& g, float cx, float cy, float radius,
                       juce::Colour colour);

    PadState* padStates = nullptr;
    int numPadStates = 0;
    int selectedPadIdx = 0;
    bool dragging = false;

    // Animated transition state
    float displayDotX = 0.5f;
    float displayDotY = 0.5f;
    float transitionProgress = 1.0f;  // 0..1, 1 = done
    float prevDotX = 0.5f, prevDotY = 0.5f;

    // Breathing animation phase
    float breathePhase = 0.0f;

    // Axis labels (customised per mode).
    juce::String labelLeft   { "LIGHT" };
    juce::String labelRight  { "WORN" };
    juce::String labelBottom { "FIXED" };
    juce::String labelTop    { "BREATHING" };

    // Ambient-engagement flag drives the outer ring in drawSelectedDot.
    bool ambientEngaged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateDotField)
};
