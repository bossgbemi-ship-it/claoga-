#include "OjuLookAndFeel.h"

namespace oju
{
using namespace theme;

OjuLookAndFeel::OjuLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, soot);
    setColour (juce::PopupMenu::backgroundColourId, iron);
    setColour (juce::PopupMenu::textColourId, bone);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, brass.withAlpha (0.35f));
    setColour (juce::TooltipWindow::backgroundColourId, iron);
    setColour (juce::TooltipWindow::textColourId, bone);
    setColour (juce::Slider::textBoxTextColourId, bone);
}

void OjuLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                       float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();
    const bool enabled = s.isEnabled();
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const bool stepped = (bool) s.getProperties().getWithDefault ("stepped", false);
    const float alpha = enabled ? 1.0f : 0.4f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // --- scale ring: groove + value arc
    const float arcR = r * 0.93f;
    const float arcW = juce::jmax (2.5f, r * 0.08f);
    juce::Path groove;
    groove.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (ink.withAlpha (alpha));
    g.strokePath (groove, juce::PathStrokeType (arcW + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (stepped)
    {
        const int steps = juce::roundToInt (s.getMaximum() - s.getMinimum());
        for (int i = 0; i <= steps; ++i)
        {
            const float a = startAngle + (float) i / (float) juce::jmax (1, steps) * (endAngle - startAngle);
            const auto p = c.getPointOnCircumference (arcR, a);
            const bool active = std::abs ((float) i - pos * (float) steps) < 0.5f;
            g.setColour ((active ? brassLight : boneDim.withAlpha (0.35f)).withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (arcW * 1.3f, arcW * 1.3f).withCentre (p));
        }
    }
    else
    {
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (brass.withAlpha (0.25f * alpha));
            g.strokePath (value, juce::PathStrokeType (arcW + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (brassLight.withAlpha (alpha));
            g.strokePath (value, juce::PathStrokeType (arcW * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // --- knurled steel skirt
    const float skirtR = r * 0.74f;
    g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
    g.fillEllipse (juce::Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c.translated (0.0f, r * 0.08f)));

    juce::ColourGradient skirt (steel.brighter (0.35f), c.x - skirtR, c.y - skirtR, ink, c.x + skirtR, c.y + skirtR, false);
    g.setGradientFill (skirt);
    g.setOpacity (alpha);
    g.fillEllipse (juce::Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c));

    g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
    const int teeth = 40;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) teeth;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (skirtR * 0.86f, a), c.getPointOnCircumference (skirtR, a)), 1.0f);
    }

    // --- machined cap
    const float capR = skirtR * 0.8f;
    const auto capBounds = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    juce::ColourGradient cap (juce::Colour (0xff5d5850), c.x - capR * 0.7f, c.y - capR * 0.9f,
                              juce::Colour (0xff1f1d1a), c.x + capR * 0.6f, c.y + capR, true);
    g.setGradientFill (cap);
    g.fillEllipse (capBounds);
    for (int i = 1; i <= 4; ++i)
    {
        g.setColour ((i % 2 ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f * alpha));
        const float rr = capR * (float) i / 4.5f;
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.8f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.12f * alpha));
    g.drawEllipse (capBounds.reduced (0.5f), 1.0f);

    // --- brass pointer
    const auto p0 = c.getPointOnCircumference (capR * 0.25f, angle);
    const auto p1 = c.getPointOnCircumference (capR * 0.92f, angle);
    g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
    g.drawLine (juce::Line<float> (p0, p1).withShortenedStart (0.0f), juce::jmax (3.0f, r * 0.09f));
    g.setColour ((s.isMouseOverOrDragging() ? brassLight : brass).withAlpha (alpha));
    g.drawLine (juce::Line<float> (p0, p1), juce::jmax (2.0f, r * 0.06f));
}

void OjuLookAndFeel::drawLamp (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool highlighted, float glow)
{
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;

    if (on && glow > 0.0f)
    {
        juce::ColourGradient halo (brassLight.withAlpha (0.35f * glow), c.x, c.y,
                                   juce::Colours::transparentBlack, c.x + rad * 1.6f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (r.expanded (rad * 0.6f));
    }

    // brass bezel
    g.setGradientFill (brassGradient (r, highlighted ? 1.15f : 1.0f));
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);

    // lens
    const auto lens = r.reduced (rad * 0.28f);
    if (on)
    {
        juce::ColourGradient lit (juce::Colour (0xfffff1c8), lens.getCentreX() - lens.getWidth() * 0.15f, lens.getCentreY() - lens.getHeight() * 0.2f,
                                  juce::Colour (0xffc7812e), lens.getRight(), lens.getBottom(), true);
        lit.addColour (0.5, juce::Colour (0xfff0b85a));
        g.setGradientFill (lit);
    }
    else
    {
        juce::ColourGradient dark (oxide.darker (0.6f), lens.getCentreX(), lens.getCentreY() - lens.getHeight() * 0.2f,
                                   ink, lens.getRight(), lens.getBottom(), true);
        g.setGradientFill (dark);
    }
    g.fillEllipse (lens);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.5f : 0.15f));
    g.fillEllipse (lens.getX() + lens.getWidth() * 0.22f, lens.getY() + lens.getHeight() * 0.16f,
                   lens.getWidth() * 0.3f, lens.getHeight() * 0.22f);
}

void OjuLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float d = juce::jmin (r.getHeight(), 24.0f);
    const auto text = b.getButtonText();

    auto lamp = text.isEmpty() ? juce::Rectangle<float> (d, d).withCentre (r.getCentre())
                               : r.removeFromLeft (d).withSizeKeepingCentre (d, d);
    drawLamp (g, lamp.reduced (1.0f), b.getToggleState(), highlighted);

    if (text.isNotEmpty())
    {
        r.removeFromLeft (8.0f);
        drawEngravedText (g, text, r, juce::Justification::centredLeft, font (15.0f),
                          b.getToggleState() ? bone : boneDim);
    }
}

} // namespace oju
