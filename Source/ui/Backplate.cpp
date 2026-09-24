#include "Backplate.h"
#include "Layout.h"

namespace oju
{
using namespace theme;

Backplate::Backplate()
{
    setInterceptsMouseClicks (false, false);
    setBufferedToImage (true);
    setOpaque (true);
}

static void panel (juce::Graphics& g, juce::Rectangle<int> area, int seed)
{
    auto r = area.toFloat();
    drawIronPlate (g, r, 10.0f, seed);
    auto inner = r.reduced (8.0f);
    drawRecess (g, inner, 7.0f);
    drawGrain (g, inner, seed + 3, 0.02f, 0.03f);
    drawBrassTrim (g, inner.expanded (1.5f), 8.0f, 1.2f);
    for (auto p : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
        drawRivet (g, p + juce::Point<float> (p.x < r.getCentreX() ? 4.5f : -4.5f, p.y < r.getCentreY() ? 4.5f : -4.5f), 2.4f);
}

void Backplate::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();

    // ---- cast iron chassis
    juce::ColourGradient base (iron, 0.0f, 0.0f, soot, 0.0f, all.getBottom(), false);
    g.setGradientFill (base);
    g.fillAll();
    {
        juce::Random rng (99);
        for (int i = 0; i < 40; ++i)
        {
            const float w = 120.0f + rng.nextFloat() * 380.0f, h = w * (0.3f + rng.nextFloat() * 0.4f);
            const float x = rng.nextFloat() * all.getWidth(), y = rng.nextFloat() * all.getHeight();
            juce::ColourGradient blob ((rng.nextFloat() < 0.3f ? oxide : juce::Colours::black).withAlpha (0.07f), x, y,
                                       juce::Colours::transparentBlack, x + w * 0.5f, y, true);
            g.setGradientFill (blob);
            g.fillEllipse (x - w * 0.5f, y - h * 0.5f, w, h);
        }
    }
    drawGrain (g, all, 5, 0.06f, 0.05f);

    // ---- brushed steel top bar with brass trim
    const auto bar = layout::topBar.toFloat();
    juce::ColourGradient steelGrad (juce::Colour (0xff45413b), 0.0f, bar.getY(), juce::Colour (0xff24211e), 0.0f, bar.getBottom(), false);
    steelGrad.addColour (0.5, juce::Colour (0xff35322d));
    g.setGradientFill (steelGrad);
    g.fillRect (bar);
    drawBrushed (g, bar, 3, 0.07f);
    g.setGradientFill (brassGradient (bar.withTop (bar.getBottom() - 3.0f)));
    g.fillRect (bar.withTop (bar.getBottom() - 3.0f));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (bar.withTop (bar.getBottom()).withHeight (2.0f));
    for (float x : { 10.0f, bar.getRight() - 10.0f })
    {
        drawRivet (g, { x, 14.0f }, 3.0f);
        drawRivet (g, { x, bar.getBottom() - 16.0f }, 3.0f);
    }

    // ---- logo: engraved brass nameplate
    {
        auto plate = layout::logo.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (plate.translated (0.0f, 2.0f), 6.0f);
        g.setGradientFill (brassGradient (plate, 0.95f));
        g.fillRoundedRectangle (plate, 6.0f);
        drawBrushed (g, plate.reduced (3.0f), 21, 0.12f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (plate, 6.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawHorizontalLine ((int) plate.getY() + 1, plate.getX() + 5.0f, plate.getRight() - 5.0f);
        drawRivet (g, { plate.getX() + 9.0f, plate.getCentreY() }, 2.6f);
        drawRivet (g, { plate.getRight() - 9.0f, plate.getCentreY() }, 2.6f);

        auto text = plate.reduced (22.0f, 4.0f);
        drawEngravedText (g, juce::String::fromUTF8 ("Oj\xc3\xba"), text.removeFromLeft (84.0f), juce::Justification::centredLeft,
                          font (38.0f, true), soot, true);
        auto sub = text.withTrimmedLeft (4.0f);
        drawEngravedText (g, "vocal chain", sub.removeFromTop (sub.getHeight() * 0.5f).translated (0.0f, 3.0f),
                          juce::Justification::centredLeft, font (15.0f, true), soot.withAlpha (0.85f), true);
        drawEngravedText (g, "Made by Joseph", sub.translated (0.0f, -2.0f), juce::Justification::centredLeft, font (13.0f), soot.withAlpha (0.7f), true);
    }

    // ---- main panels
    panel (g, layout::eyePanel, 10);
    panel (g, layout::eqPanel, 20);
    panel (g, layout::readPanel, 30);
    panel (g, layout::meterPanel, 40);

    // ---- module cards
    const char* titles[] = { "Shape", "Tame", "Press", "Heat", "Space", "Master" };
    for (int i = 0; i < layout::numCards; ++i)
    {
        const auto r = layout::card (i).toFloat();
        drawIronPlate (g, r, 9.0f, 100 + i);

        auto header = r.withHeight ((float) layout::cardHeader).reduced (10.0f, 0.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawHorizontalLine ((int) header.getBottom(), r.getX() + 8.0f, r.getRight() - 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) header.getBottom() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);

        // small brass tab behind the title
        g.setColour (brass.withAlpha (0.85f));
        g.fillRoundedRectangle (header.getX() + 2.0f, header.getCentreY() - 7.0f, 3.0f, 14.0f, 1.5f);
        drawEngravedText (g, titles[i], header.withTrimmedLeft (12.0f), juce::Justification::centredLeft, font (19.0f, true), bone);

        for (auto p : { r.getBottomLeft(), r.getBottomRight() })
            drawRivet (g, p + juce::Point<float> (p.x < r.getCentreX() ? 8.0f : -8.0f, -8.0f), 2.2f);
    }
}

} // namespace oju
