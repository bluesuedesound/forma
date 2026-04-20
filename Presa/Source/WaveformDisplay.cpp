#include "WaveformDisplay.h"

WaveformDisplay::WaveformDisplay()
{
    startTimerHz (30);
}

void WaveformDisplay::setWaveform (const float* samples, int numSamples)
{
    rawSampleCount = numSamples;

    // Downsample to ~400 peaks for display
    int peakCount = 400;
    waveformPeaks.resize ((size_t) peakCount);
    int samplesPerPeak = juce::jmax (1, numSamples / peakCount);

    for (int i = 0; i < peakCount; ++i)
    {
        float peak = 0.0f;
        int start = i * samplesPerPeak;
        int end = juce::jmin (start + samplesPerPeak, numSamples);
        for (int s = start; s < end; ++s)
            peak = juce::jmax (peak, std::abs (samples[s]));
        waveformPeaks[(size_t) i] = peak;
    }
    repaint();
}

void WaveformDisplay::clearWaveform()
{
    waveformPeaks.clear();
    rawSampleCount = 0;
    repaint();
}

void WaveformDisplay::setSliceRange (float start, float end)
{
    sliceStart = start;
    sliceEnd   = end;
    repaint();
}

void WaveformDisplay::setBreathing (float amount) { breathingAmount = amount; }
void WaveformDisplay::setWorn (float amount)      { wornAmount = amount; }
void WaveformDisplay::setScatterRange (float range) { scatterRange = range; }

void WaveformDisplay::setSampleMode (bool on)
{
    if (sampleMode == on) return;
    sampleMode = on;
    setMouseCursor (on ? juce::MouseCursor::UpDownResizeCursor
                       : juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformDisplay::setLoopingVisible (bool on)
{
    if (loopingVisible == on) return;
    loopingVisible = on;
    repaint();
}

void WaveformDisplay::setReversedVisible (bool on)
{
    if (reversedVisible == on) return;
    reversedVisible = on;
    repaint();
}

void WaveformDisplay::setPitchOverlay (int semitones)
{
    pitchOverlaySemis = semitones;
    pitchOverlayAlpha = 1.0f;
    repaint();
}

// ══════════════════════════════════════════════════════════════════════════
// Timer
// ══════════════════════════════════════════════════════════════════════════

void WaveformDisplay::timerCallback()
{
    float dt = 1.0f / 30.0f;
    breathePhase += dt * 0.5f;
    if (breathePhase > juce::MathConstants<float>::twoPi)
        breathePhase -= juce::MathConstants<float>::twoPi;

    // Pitch overlay: full alpha during a drag, otherwise fade to zero over 1 s.
    if (pitchOverlayAlpha > 0.0f && currentDrag != PitchVertical)
    {
        pitchOverlayAlpha -= dt;
        if (pitchOverlayAlpha < 0.0f) pitchOverlayAlpha = 0.0f;
        repaint();
    }

    if (breathingAmount > 0.01f || wornAmount > 0.01f)
        repaint();
}

// ══════════════════════════════════════════════════════════════════════════
// Paint
// ══════════════════════════════════════════════════════════════════════════

void WaveformDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour (Palette::BG());
    g.fillRect (bounds);

    // Grid lines
    g.setColour (Palette::BORDER());
    int numGridLines = 8;
    for (int i = 1; i < numGridLines; ++i)
    {
        float x = bounds.getX() + bounds.getWidth() * i / (float) numGridLines;
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }
    // Centre horizontal line
    float midY = bounds.getCentreY();
    g.drawHorizontalLine ((int) midY, bounds.getX(), bounds.getRight());

    auto waveArea = bounds.reduced (4.0f, 8.0f);

    // Pre-roll region (before slice start)
    if (sliceStart > 0.0f)
    {
        auto preRoll = waveArea.withRight (waveArea.getX() + sliceStart * waveArea.getWidth());
        g.setColour (Palette::DARK());
        g.fillRect (preRoll);
    }

    // Post-roll region (after slice end)
    if (sliceEnd < 1.0f)
    {
        auto postRoll = waveArea.withLeft (waveArea.getX() + sliceEnd * waveArea.getWidth());
        g.setColour (Palette::DARK());
        g.fillRect (postRoll);
    }

    drawScatterRange (g, waveArea);
    drawWaveform (g, waveArea);

    if (wornAmount > 0.5f)
        drawGrainOverlay (g, waveArea);

    if (breathingAmount > 0.5f)
        drawBreathingDeform (g, waveArea);

    // Dim the audio outside the trim window so the active region reads as
    // the bright strip between the two handles. Drawn after the waveform
    // and before the handles so the handle ellipses remain crisp on top.
    {
        const float leftX  = waveArea.getX() + sliceStart * waveArea.getWidth();
        const float rightX = waveArea.getX() + sliceEnd   * waveArea.getWidth();
        const auto dim     = Palette::DARK().withAlpha (0.5f);

        g.setColour (dim);
        if (sliceStart > 0.0f)
            g.fillRect (waveArea.getX(), waveArea.getY(),
                        leftX - waveArea.getX(), waveArea.getHeight());
        if (sliceEnd < 1.0f)
            g.fillRect (rightX, waveArea.getY(),
                        waveArea.getRight() - rightX, waveArea.getHeight());
    }

    // Loop-region highlight — only visible in SAMPLE mode with LOOP engaged.
    if (sampleMode && loopingVisible)
    {
        const float lx = waveArea.getX() + sliceStart * waveArea.getWidth();
        const float rx = waveArea.getX() + sliceEnd   * waveArea.getWidth();
        g.setColour (Palette::ACCENT().withAlpha (0.15f));
        g.fillRect (lx, waveArea.getY(), rx - lx, waveArea.getHeight());
    }

    drawSliceHandles (g, waveArea);

    // Reverse-direction arrow inside the trim window, SAMPLE mode only.
    if (sampleMode && reversedVisible)
    {
        const float cx = waveArea.getCentreX();
        const float cy = waveArea.getY() + 14.0f;
        const float w  = 10.0f;
        const float h  = 6.0f;

        juce::Path arrow;
        arrow.addTriangle (cx + w, cy - h,
                           cx + w, cy + h,
                           cx - w, cy);
        g.setColour (Palette::ACCENT());
        g.fillPath (arrow);
    }

    // Pitch overlay centred large, fades after 1 s.
    if (sampleMode && pitchOverlayAlpha > 0.0f)
    {
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                               48.0f, juce::Font::plain));
        g.setColour (Palette::TXT_HI().withAlpha (pitchOverlayAlpha));

        juce::String t = (pitchOverlaySemis > 0 ? "+" : "")
                       + juce::String (pitchOverlaySemis) + " st";
        g.drawText (t, waveArea, juce::Justification::centred, false);
    }
}

// ── Waveform ───────────────────────────────────────────────────────────────

void WaveformDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    if (waveformPeaks.empty())
    {
        // Flat line when no sample loaded
        g.setColour (Palette::ACCENT().withAlpha (0.3f));
        float y = bounds.getCentreY();
        g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
        return;
    }

    g.setColour (Palette::ACCENT().withAlpha (0.6f));

    juce::Path path;
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float midY = bounds.getCentreY();

    for (size_t i = 0; i < waveformPeaks.size(); ++i)
    {
        float x = bounds.getX() + (float) i / (float) waveformPeaks.size() * w;
        float peak = waveformPeaks[i] * h * 0.45f;

        if (i == 0)
            path.startNewSubPath (x, midY - peak);
        else
            path.lineTo (x, midY - peak);
    }

    // Mirror bottom
    for (int i = (int) waveformPeaks.size() - 1; i >= 0; --i)
    {
        float x = bounds.getX() + (float) i / (float) waveformPeaks.size() * w;
        float peak = waveformPeaks[(size_t) i] * h * 0.45f;
        path.lineTo (x, midY + peak);
    }

    path.closeSubPath();
    g.fillPath (path);
}

// ── Slice handles ──────────────────────────────────────────────────────────

void WaveformDisplay::drawSliceHandles (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    float startX = bounds.getX() + sliceStart * bounds.getWidth();
    float endX   = bounds.getX() + sliceEnd   * bounds.getWidth();

    // Handle lines
    g.setColour (Palette::ACCENT());
    g.drawVerticalLine ((int) startX, bounds.getY(), bounds.getBottom());
    g.drawVerticalLine ((int) endX,   bounds.getY(), bounds.getBottom());

    // Glow rings on handles
    for (float hx : { startX, endX })
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            float r = 4.0f + pass * 2.0f;
            float alpha = 0.15f - pass * 0.04f;
            g.setColour (Palette::ACCENT().withAlpha (alpha));
            g.drawEllipse (hx - r, bounds.getCentreY() - r, r * 2.0f, r * 2.0f, 1.5f);
        }
        g.setColour (Palette::ACCENT());
        g.fillEllipse (hx - 3.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
}

// ── Scatter range ──────────────────────────────────────────────────────────

void WaveformDisplay::drawScatterRange (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    if (scatterRange < 0.01f) return;

    float startX = bounds.getX() + sliceStart * bounds.getWidth();
    float rangeW = scatterRange * (sliceEnd - sliceStart) * bounds.getWidth();

    g.setColour (Palette::ACCENT_DK().withAlpha (0.25f));
    g.fillRect (startX, bounds.getY(), rangeW, bounds.getHeight());
}

// ── Grain overlay ──────────────────────────────────────────────────────────

void WaveformDisplay::drawGrainOverlay (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    float intensity = (wornAmount - 0.5f) * 2.0f;  // 0..1 in the WORN > 50% range
    int numGrains = (int) (intensity * 200.0f);
    g.setColour (Palette::TXT_GHOST().withAlpha (intensity * 0.3f));

    for (int i = 0; i < numGrains; ++i)
    {
        float gx = bounds.getX() + grainRng.nextFloat() * bounds.getWidth();
        float gy = bounds.getY() + grainRng.nextFloat() * bounds.getHeight();
        g.fillRect (gx, gy, 1.0f, 1.0f);
    }
}

// ── Breathing deformation ──────────────────────────────────────────────────

void WaveformDisplay::drawBreathingDeform (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    float intensity = (breathingAmount - 0.5f) * 2.0f;
    g.setColour (Palette::ACCENT().withAlpha (intensity * 0.08f));

    juce::Path deform;
    float midY = bounds.getCentreY();

    for (int i = 0; i < (int) bounds.getWidth(); i += 2)
    {
        float nx = (float) i / bounds.getWidth();
        // Multi-frequency breathing (matching Forma design language)
        float wave = std::sin (breathePhase * 3.0f + nx * 8.0f) * 0.3f
                   + std::sin (breathePhase * 5.0f + nx * 12.0f) * 0.2f
                   + std::sin (breathePhase * 2.0f + nx * 4.0f) * 0.5f;
        float y = midY + wave * bounds.getHeight() * 0.15f * intensity;

        if (i == 0)
            deform.startNewSubPath (bounds.getX(), y);
        else
            deform.lineTo (bounds.getX() + (float) i, y);
    }

    g.strokePath (deform, juce::PathStrokeType (1.5f));
}

// ══════════════════════════════════════════════════════════════════════════
// Mouse — handle dragging
// ══════════════════════════════════════════════════════════════════════════

void WaveformDisplay::mouseDown (const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat().reduced (4.0f, 8.0f);
    float mx = (e.position.x - bounds.getX()) / bounds.getWidth();

    float startDist = std::abs (mx - sliceStart);
    float endDist   = std::abs (mx - sliceEnd);
    float threshold = 0.02f;

    if (startDist < threshold && startDist < endDist)
    {
        currentDrag = StartHandle;
        dragOffset = mx - sliceStart;
    }
    else if (endDist < threshold)
    {
        currentDrag = EndHandle;
        dragOffset = mx - sliceEnd;
    }
    else if (sampleMode)
    {
        // SAMPLE mode: any click not on a handle initiates a vertical pitch
        // drag. Region-drag is intentionally disabled here so the waveform's
        // primary gesture is pitch, matching the design.
        currentDrag     = PitchVertical;
        pitchDragStartY = e.position.getY();
        lastEmittedDelta = 0;
        if (onPitchDragStart) onPitchDragStart();
    }
    else if (mx > sliceStart && mx < sliceEnd)
    {
        currentDrag = Region;
        dragOffset = mx - sliceStart;
    }
}

void WaveformDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (currentDrag == None) return;

    if (currentDrag == PitchVertical)
    {
        // 10 px per semitone; drag up raises pitch.
        const int dyPx  = e.position.getY() - pitchDragStartY;
        const int delta = -dyPx / 10;
        if (delta != lastEmittedDelta)
        {
            lastEmittedDelta = delta;
            if (onPitchDelta) onPitchDelta (delta);
        }
        return;
    }

    auto bounds = getLocalBounds().toFloat().reduced (4.0f, 8.0f);
    float mx = juce::jlimit (0.0f, 1.0f,
                             (e.position.x - bounds.getX()) / bounds.getWidth());

    switch (currentDrag)
    {
        case StartHandle:
            sliceStart = juce::jlimit (0.0f, sliceEnd - 0.01f, mx - dragOffset);
            break;
        case EndHandle:
            sliceEnd = juce::jlimit (sliceStart + 0.01f, 1.0f, mx - dragOffset);
            break;
        case Region:
        {
            float len = sliceEnd - sliceStart;
            float newStart = juce::jlimit (0.0f, 1.0f - len, mx - dragOffset);
            sliceStart = newStart;
            sliceEnd   = newStart + len;
            break;
        }
        default: break;
    }

    if (onSliceChanged)
        onSliceChanged (sliceStart, sliceEnd);

    repaint();
}

void WaveformDisplay::mouseUp (const juce::MouseEvent&)
{
    if (currentDrag == PitchVertical && onPitchDragEnd)
        onPitchDragEnd();
    currentDrag = None;
}
