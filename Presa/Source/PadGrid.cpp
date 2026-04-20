#include "PadGrid.h"
#include "PluginProcessor.h"

static juce::Colour lerpColour (juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t));
}

PadGrid::PadGrid() {}

void PadGrid::setPadStates (PadState* padArray, int n)
{
    padStates    = padArray;
    numPadStates = n;
}

void PadGrid::setSelectedPad (int padIndex)
{
    selectedPadIdx = padIndex;
    repaint();
}

void PadGrid::setActivePad (int padIndex)
{
    activePadIdx = padIndex;
    repaint();
}

// ══════════════════════════════════════════════════════════════════════════
// Layout
// ══════════════════════════════════════════════════════════════════════════

void PadGrid::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour (Palette::BG2());
    g.fillRect (bounds);

    int gap = 3;
    int padW = (bounds.getWidth()  - gap * (kCols + 1)) / kCols;
    int padH = (bounds.getHeight() - gap * (kRows + 1)) / kRows;

    for (int i = 0; i < kNumPads; ++i)
    {
        int col = i % kCols;
        int row = i / kCols;
        int x = bounds.getX() + gap + col * (padW + gap);
        int y = bounds.getY() + gap + row * (padH + gap);
        padRects[i] = { x, y, padW, padH };
        drawPad (g, i, padRects[i]);
    }
}

void PadGrid::drawPad (juce::Graphics& g, int padIndex, juce::Rectangle<int> bounds)
{
    bool isSelected = (padIndex == selectedPadIdx);
    bool isActive   = (padIndex == activePadIdx);

    // Fill
    if (isActive)
    {
        g.setColour (Palette::ACCENT());
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        // Glow
        g.setColour (Palette::ACCENT().withAlpha (0.12f));
        g.fillRoundedRectangle (bounds.toFloat().expanded (2.0f), 7.0f);
    }
    else
    {
        g.setColour (Palette::DARK());
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
    }

    // Border
    if (isSelected)
    {
        g.setColour (Palette::BORDER2());
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 2.0f);
    }
    else
    {
        g.setColour (Palette::ACCENT_DK());
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    auto mono = juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain);
    g.setFont (mono);

    // Pad number — top-left (always visible)
    g.setColour (isSelected ? Palette::TXT_HI() : Palette::TXT_MID());
    g.drawText (juce::String (padIndex + 1),
                bounds.reduced (4, 2),
                juce::Justification::topLeft, false);

    // Mode label — bottom-right
    if (padStates != nullptr && padIndex < numPadStates)
    {
        g.setColour (isSelected ? Palette::TXT_MID() : Palette::TXT_DIM());
        g.drawText (padModeLabel (padStates[padIndex].mode),
                    bounds.reduced (4, 2),
                    juce::Justification::bottomRight, false);

        // Quadrant dot indicator — 3px coloured circle, top-right
        auto dotColour = getQuadrantColour (padStates[padIndex].dotX,
                                            padStates[padIndex].dotY);
        g.setColour (dotColour);
        g.fillEllipse ((float) bounds.getRight() - 9.0f,
                       (float) bounds.getY() + 4.0f,
                       3.0f, 3.0f);
    }
}

juce::Colour PadGrid::getQuadrantColour (float dotX, float dotY) const
{
    // Blend the four quadrant outer colours
    auto bl = Palette::NOCTURNE_OUTER();
    auto tl = Palette::HOLLOW_OUTER();
    auto br = Palette::WARM_OUTER();
    auto tr = Palette::TENSE_OUTER();

    auto top    = lerpColour (tl, tr, dotX);
    auto bottom = lerpColour (bl, br, dotX);
    return lerpColour (bottom, top, dotY);
}

// ══════════════════════════════════════════════════════════════════════════
// Mouse
// ══════════════════════════════════════════════════════════════════════════

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumPads; ++i)
    {
        if (padRects[i].contains (e.getPosition()))
        {
            if (e.mods.isRightButtonDown() || e.mods.isCtrlDown())
            {
                if (onPadRightClick)
                    onPadRightClick (i);
            }
            else
            {
                selectedPadIdx = i;
                if (onPadSelected)
                    onPadSelected (i);
            }
            repaint();
            return;
        }
    }
}

void PadGrid::mouseUp (const juce::MouseEvent&) {}
