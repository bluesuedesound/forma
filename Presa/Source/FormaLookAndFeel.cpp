#include "FormaLookAndFeel.h"

FormaLookAndFeel::FormaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::BG());
    setColour (juce::TextButton::buttonColourId,          Palette::DARK());
    setColour (juce::TextButton::buttonOnColourId,        Palette::ACCENT());
    setColour (juce::TextButton::textColourOffId,         Palette::TXT_MID());
    setColour (juce::TextButton::textColourOnId,          Palette::TXT_HI());
    setColour (juce::Label::textColourId,                 Palette::TXT_MID());
}

void FormaLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                         int x, int y, int w, int h,
                                         float sliderPos,
                                         float rotaryStartAngle,
                                         float rotaryEndAngle,
                                         juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto arcRadius = radius - 4.0f;
    auto lineW = 2.5f;

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                         0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Palette::DARK());
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    // Fill arc
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    juce::Path fill;
    fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius,
                        0.0f, rotaryStartAngle, toAngle, true);
    g.setColour (Palette::ACCENT());
    g.strokePath (fill, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Glow on active
    if (slider.isMouseOverOrDragging())
    {
        g.setColour (Palette::ACCENT().withAlpha (0.15f));
        g.strokePath (fill, juce::PathStrokeType (lineW + 4.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Thumb dot
    auto thumbAngle = toAngle;
    auto thumbX = centre.x + arcRadius * std::cos (thumbAngle - juce::MathConstants<float>::halfPi);
    auto thumbY = centre.y + arcRadius * std::sin (thumbAngle - juce::MathConstants<float>::halfPi);
    g.setColour (Palette::TXT_HI());
    g.fillEllipse (thumbX - 3.0f, thumbY - 3.0f, 6.0f, 6.0f);
}

void FormaLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                             juce::Button& button,
                                             const juce::Colour&,
                                             bool highlighted,
                                             bool down)
{
    auto bounds = button.getLocalBounds().toFloat();
    bool on = button.getToggleState();

    // Fill
    if (on)
        g.setColour (down ? Palette::ACCENT().darker (0.2f) : Palette::ACCENT());
    else
        g.setColour (down ? Palette::DARK().brighter (0.05f) : Palette::DARK());

    g.fillRoundedRectangle (bounds, 4.0f);

    // 1px border
    g.setColour (Palette::BORDER());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Highlight glow
    if (highlighted && !on)
    {
        g.setColour (Palette::ACCENT().withAlpha (0.08f));
        g.fillRoundedRectangle (bounds, 4.0f);
    }
}

void FormaLookAndFeel::drawToggleButton (juce::Graphics& g,
                                         juce::ToggleButton& button,
                                         bool highlighted,
                                         bool /*down*/)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto switchW = 28.0f;
    auto switchH = 16.0f;
    auto switchX = bounds.getX() + 4.0f;
    auto switchY = bounds.getCentreY() - switchH * 0.5f;
    auto switchBounds = juce::Rectangle<float> (switchX, switchY, switchW, switchH);

    bool on = button.getToggleState();

    // Track
    g.setColour (on ? Palette::ACCENT() : Palette::DARK());
    g.fillRoundedRectangle (switchBounds, 8.0f);

    // Thumb
    auto thumbR = switchH - 4.0f;
    auto thumbX = on ? (switchX + switchW - thumbR - 2.0f) : (switchX + 2.0f);
    auto thumbY2 = switchY + 2.0f;
    g.setColour (Palette::TXT_HI());
    g.fillEllipse (thumbX, thumbY2, thumbR, thumbR);

    // Label
    auto textArea = bounds.withLeft (switchX + switchW + 6.0f);
    g.setColour (on ? Palette::TXT_MID() : Palette::TXT_DIM());
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    g.drawText (button.getButtonText(), textArea.toNearestInt(),
                juce::Justification::centredLeft, false);
}

juce::Font FormaLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain);
}
