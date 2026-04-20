#include "StateDotField.h"
#include "PluginProcessor.h"  // for PadState

// ── Helpers ────────────────────────────────────────────────────────────────

static juce::Colour lerpColour (juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t));
}

// Cubic ease (smoothstep)
static float smoothstep (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

// Bilinear blend of four quadrant colours
static juce::Colour quadrantBlend (float x, float y,
                                    juce::Colour bottomLeft,
                                    juce::Colour topLeft,
                                    juce::Colour bottomRight,
                                    juce::Colour topRight)
{
    auto top    = lerpColour (topLeft,    topRight,    x);
    auto bottom = lerpColour (bottomLeft, bottomRight, x);
    return lerpColour (bottom, top, y);
}

// ══════════════════════════════════════════════════════════════════════════
// Construction
// ══════════════════════════════════════════════════════════════════════════

StateDotField::StateDotField()
{
    startTimerHz (60);
}

void StateDotField::setPadStates (PadState* padArray, int n)
{
    padStates    = padArray;
    numPadStates = n;
}

void StateDotField::setAxisLabels (juce::String left, juce::String right,
                                   juce::String bottom, juce::String top)
{
    if (labelLeft == left && labelRight == right
     && labelBottom == bottom && labelTop == top)
        return;

    labelLeft   = std::move (left);
    labelRight  = std::move (right);
    labelBottom = std::move (bottom);
    labelTop    = std::move (top);
    repaint();
}

void StateDotField::setAmbientEngaged (bool engaged)
{
    if (ambientEngaged == engaged) return;
    ambientEngaged = engaged;
    repaint();
}

void StateDotField::setSelectedPad (int padIndex)
{
    if (padIndex == selectedPadIdx || padStates == nullptr)
        return;

    // Begin transition animation
    prevDotX = displayDotX;
    prevDotY = displayDotY;
    transitionProgress = 0.0f;
    selectedPadIdx = padIndex;
    repaint();
}

// ══════════════════════════════════════════════════════════════════════════
// Timer — animation
// ══════════════════════════════════════════════════════════════════════════

void StateDotField::timerCallback()
{
    float dt = 1.0f / 60.0f;
    bool dirty = false;

    // Transition animation (400ms cubic ease)
    if (transitionProgress < 1.0f)
    {
        transitionProgress += dt / 0.4f;
        if (transitionProgress > 1.0f) transitionProgress = 1.0f;
        dirty = true;
    }

    // Update display dot position
    if (padStates != nullptr && selectedPadIdx >= 0 && selectedPadIdx < numPadStates)
    {
        float targetX = padStates[selectedPadIdx].dotX;
        float targetY = padStates[selectedPadIdx].dotY;

        if (transitionProgress < 1.0f)
        {
            float t = smoothstep (transitionProgress);
            displayDotX = prevDotX + (targetX - prevDotX) * t;
            displayDotY = prevDotY + (targetY - prevDotY) * t;
        }
        else
        {
            displayDotX = targetX;
            displayDotY = targetY;
        }
    }

    // Breathing phase
    breathePhase += dt * 0.7f;
    if (breathePhase > juce::MathConstants<float>::twoPi)
        breathePhase -= juce::MathConstants<float>::twoPi;
    dirty = true; // always repaint for breathing

    if (dirty)
        repaint();
}

// ══════════════════════════════════════════════════════════════════════════
// Mouse interaction
// ══════════════════════════════════════════════════════════════════════════

void StateDotField::mouseDown (const juce::MouseEvent& e)
{
    if (padStates == nullptr) return;

    auto bounds = getLocalBounds().toFloat().reduced (20.0f);
    float nx = (e.position.x - bounds.getX()) / bounds.getWidth();
    float ny = 1.0f - (e.position.y - bounds.getY()) / bounds.getHeight();

    // Check if near selected dot
    float dx = nx - displayDotX;
    float dy = ny - displayDotY;
    if (std::sqrt (dx * dx + dy * dy) < 0.08f)
    {
        dragging = true;
        transitionProgress = 1.0f;
    }
}

void StateDotField::mouseDrag (const juce::MouseEvent& e)
{
    if (!dragging || padStates == nullptr) return;

    auto bounds = getLocalBounds().toFloat().reduced (20.0f);
    float nx = juce::jlimit (0.0f, 1.0f,
                             (e.position.x - bounds.getX()) / bounds.getWidth());
    float ny = juce::jlimit (0.0f, 1.0f,
                             1.0f - (e.position.y - bounds.getY()) / bounds.getHeight());

    padStates[selectedPadIdx].dotX = nx;
    padStates[selectedPadIdx].dotY = ny;
    displayDotX = nx;
    displayDotY = ny;

    if (onDotMoved)
        onDotMoved (selectedPadIdx, nx, ny);

    repaint();
}

void StateDotField::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

// ══════════════════════════════════════════════════════════════════════════
// Paint
// ══════════════════════════════════════════════════════════════════════════

void StateDotField::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour (Palette::BG2());
    g.fillRect (bounds);

    auto field = bounds.reduced (20.0f);

    drawThermalGradient (g, field, displayDotX, displayDotY);
    drawGhostDots (g, field);
    drawSelectedDot (g, field);
    drawAxisLabels (g, bounds);
}

// ── Thermal gradient ───────────────────────────────────────────────────────

void StateDotField::drawThermalGradient (juce::Graphics& g,
                                         juce::Rectangle<float> bounds,
                                         float dotX, float dotY)
{
    // Four quadrant colours
    auto bottomLeft  = Palette::NOCTURNE_INNER();  // LIGHT + FIXED
    auto topLeft     = Palette::HOLLOW_INNER();     // LIGHT + BREATHING
    auto bottomRight = Palette::WARM_INNER();       // WORN + FIXED
    auto topRight    = Palette::TENSE_INNER();      // WORN + BREATHING

    auto blOuter = Palette::NOCTURNE_OUTER();
    auto tlOuter = Palette::HOLLOW_OUTER();
    auto brOuter = Palette::WARM_OUTER();
    auto trOuter = Palette::TENSE_OUTER();

    // Draw the gradient as a grid of small rects for smooth blending
    int steps = 32;
    float cellW = bounds.getWidth()  / (float) steps;
    float cellH = bounds.getHeight() / (float) steps;

    for (int iy = 0; iy < steps; ++iy)
    {
        float ny = (float) iy / (float) (steps - 1);
        float yFlipped = 1.0f - ny;  // top = BREATHING (1), bottom = FIXED (0)

        for (int ix = 0; ix < steps; ++ix)
        {
            float nx = (float) ix / (float) (steps - 1);

            // Base colour from quadrant blend (inner colours)
            auto inner = quadrantBlend (nx, yFlipped, bottomLeft, topLeft, bottomRight, topRight);
            auto outer = quadrantBlend (nx, yFlipped, blOuter, tlOuter, brOuter, trOuter);

            // Distance from dot position affects inner/outer blend
            float ddx = nx - dotX;
            float ddy = yFlipped - dotY;
            float dist = std::sqrt (ddx * ddx + ddy * ddy);

            // Glow radius around dot — radial falloff
            float glowT = juce::jlimit (0.0f, 1.0f, dist * 2.5f);
            auto colour = lerpColour (outer, inner, glowT);

            // Add breathing modulation near high-Y regions.
            // sin() goes both positive and negative — brighter() asserts on
            // negative input, so split into brighten/darken halves.
            if (dotY > 0.5f)
            {
                float breatheMod = std::sin (breathePhase + nx * 3.0f) * 0.02f * (dotY - 0.5f) * 2.0f;
                colour = (breatheMod >= 0.0f)
                       ? colour.brighter (breatheMod)
                       : colour.darker (-breatheMod);
            }

            g.setColour (colour);
            g.fillRect (bounds.getX() + ix * cellW,
                       bounds.getY() + iy * cellH,
                       cellW + 1.0f, cellH + 1.0f);
        }
    }
}

// ── Ghost dots ─────────────────────────────────────────────────────────────

void StateDotField::drawGhostDots (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    if (padStates == nullptr) return;

    g.setColour (Palette::TXT_GHOST());

    for (int i = 0; i < numPadStates; ++i)
    {
        if (i == selectedPadIdx) continue;

        float px = bounds.getX() + padStates[i].dotX * bounds.getWidth();
        float py = bounds.getY() + (1.0f - padStates[i].dotY) * bounds.getHeight();
        g.fillEllipse (px - 2.0f, py - 2.0f, 4.0f, 4.0f);
    }
}

// ── Selected dot with glow ring ────────────────────────────────────────────

void StateDotField::drawSelectedDot (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    float px = bounds.getX() + displayDotX * bounds.getWidth();
    float py = bounds.getY() + (1.0f - displayDotY) * bounds.getHeight();

    drawGlowRing (g, px, py, 8.0f, Palette::ACCENT());

    // Extra outer ring when the AmbientEngine is actively producing feedback —
    // gives the SAMPLE-mode dot a halo that reads as "something is evolving".
    if (ambientEngaged)
    {
        const float r = 20.0f;
        g.setColour (Palette::ACCENT().withAlpha (0.08f));
        g.drawEllipse (px - r, py - r, r * 2.0f, r * 2.0f, 1.5f);
    }

    // Centre dot
    g.setColour (Palette::ACCENT());
    g.fillEllipse (px - 5.0f, py - 5.0f, 10.0f, 10.0f);
}

void StateDotField::drawGlowRing (juce::Graphics& g, float cx, float cy,
                                   float radius, juce::Colour colour)
{
    // 3-pass expanding glow
    for (int pass = 0; pass < 3; ++pass)
    {
        float r = radius + (pass + 1) * 3.0f;
        float alpha = 0.15f - pass * 0.04f;
        g.setColour (colour.withAlpha (alpha));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
    }
}

// ── Axis labels ────────────────────────────────────────────────────────────

void StateDotField::drawAxisLabels (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    auto mono = juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain);
    g.setFont (mono);
    g.setColour (Palette::TXT_DIM());

    auto field = bounds.reduced (20.0f);
    int fieldY = (int) field.getY();
    int fieldB = (int) field.getBottom();
    int fieldX = (int) field.getX();
    int fieldR = (int) field.getRight();

    // X axis labels flank the bottom edge; Y axis labels stack at left edge.
    g.drawText (labelLeft,  fieldX,          fieldB + 4, 60, 12, juce::Justification::centredLeft,  false);
    g.drawText (labelRight, fieldR - 60,     fieldB + 4, 60, 12, juce::Justification::centredRight, false);
    g.drawText (labelBottom, fieldX - 2,     fieldB - 14, 72, 12, juce::Justification::centredLeft, false);
    g.drawText (labelTop,    fieldX - 2,     fieldY + 6,  72, 12, juce::Justification::centredLeft, false);
}
