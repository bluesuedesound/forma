#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "FormaPalette.h"

struct PadState;
enum class PadMode;

class PadGrid : public juce::Component
{
public:
    static constexpr int kNumPads = 16;
    static constexpr int kCols    = 4;
    static constexpr int kRows    = 4;

    PadGrid();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    void setPadStates (PadState* padArray, int numPads);
    void setSelectedPad (int padIndex);
    void setActivePad (int padIndex);  // currently playing (trigger highlight)

    // Callback when user clicks a pad
    std::function<void (int padIndex)> onPadSelected;

    // Callback for right-click (mode cycling)
    std::function<void (int padIndex)> onPadRightClick;

private:
    PadState* padStates = nullptr;
    int numPadStates = 0;
    int selectedPadIdx = 0;
    int activePadIdx = -1;

    juce::Rectangle<int> padRects[kNumPads];

    void drawPad (juce::Graphics& g, int padIndex, juce::Rectangle<int> bounds);

    // Get quadrant colour for a pad's dot position
    juce::Colour getQuadrantColour (float dotX, float dotY) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};
