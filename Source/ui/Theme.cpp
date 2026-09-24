#include "Theme.h"

namespace oju::theme
{
namespace
{
    juce::String pickFamily()
    {
        const auto available = juce::Font::findAllTypefaceNames();
        const char* candidates[] = {
           #if JUCE_WINDOWS
            "Bahnschrift", "Bahnschrift SemiCondensed", "Segoe UI", "Arial"
           #elif JUCE_MAC
            "Avenir Next Condensed", "Avenir Next", "Helvetica Neue", "Helvetica"
           #else
            "Bahnschrift", "Avenir Next Condensed", "DejaVu Sans Condensed", "Liberation Sans Narrow",
            "DejaVu Sans", "Liberation Sans"
           #endif
        };
        for (auto* c : candidates)
            if (available.contains (c))
                return c;
        return juce::Font::getDefaultSansSerifFontName();
    }
}

juce::Font font (float height, bool bold)
{
    static const juce::String family = pickFamily();
    return juce::Font (juce::FontOptions (family, height, bold ? juce::Font::bold : juce::Font::plain));
}

void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Justification just, const juce::Font& f, juce::Colour colour, bool onLightMetal)
{
    g.setFont (f);
    // cut into the metal: shadow on the upper edge, catch-light on the lower edge
    g.setColour (juce::Colours::black.withAlpha (onLightMetal ? 0.35f : 0.75f));
    g.drawText (text, area.translated (0.0f, -1.0f), just, false);
    g.setColour (juce::Colours::white.withAlpha (onLightMetal ? 0.25f : 0.07f));
    g.drawText (text, area.translated (0.0f, 1.0f), just, false);
    g.setColour (colour);
    g.drawText (text, area, just, false);
}

void drawRivet (juce::Graphics& g, juce::Point<float> c, float r)
{
    // seat shadow
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - r * 1.25f, c.y - r * 1.1f, r * 2.5f, r * 2.5f);
    // domed head
    juce::ColourGradient dome (steel.brighter (0.5f), c.x - r * 0.5f, c.y - r * 0.6f,
                               ink, c.x + r * 0.8f, c.y + r * 0.9f, true);
    dome.addColour (0.45, steel);
    g.setGradientFill (dome);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.6f, r * 0.55f, r * 0.45f);
}

void drawGrain (juce::Graphics& g, juce::Rectangle<float> area, int seed, float density, float alpha)
{
    juce::Random rng (seed);
    const int count = (int) (area.getWidth() * area.getHeight() * density);
    for (int i = 0; i < count; ++i)
    {
        const float x = area.getX() + rng.nextFloat() * area.getWidth();
        const float y = area.getY() + rng.nextFloat() * area.getHeight();
        const bool light = rng.nextBool();
        g.setColour ((light ? bone : juce::Colours::black).withAlpha (alpha * (0.4f + rng.nextFloat())));
        const float s = 0.6f + rng.nextFloat() * 0.9f;
        g.fillRect (x, y, s, s);
    }
}

void drawBrushed (juce::Graphics& g, juce::Rectangle<float> area, int seed, float alpha)
{
    juce::Random rng (seed);
    for (float y = area.getY(); y < area.getBottom(); y += 0.75f + rng.nextFloat() * 0.9f)
    {
        const float x0 = area.getX() + rng.nextFloat() * area.getWidth() * 0.3f;
        const float x1 = area.getRight() - rng.nextFloat() * area.getWidth() * 0.3f;
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (alpha * rng.nextFloat()));
        g.drawHorizontalLine ((int) y, x0, x1);
    }
}

void drawIronPlate (juce::Graphics& g, juce::Rectangle<float> r, float corner, int seed)
{
    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (1.0f), corner + 1.0f);

    juce::ColourGradient body (ironLight, r.getX(), r.getY(), iron.darker (0.25f), r.getX(), r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, corner);

    // cast mottling
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
        juce::Random rng (seed);
        for (int i = 0; i < 14; ++i)
        {
            const float w = r.getWidth() * (0.15f + rng.nextFloat() * 0.35f);
            const float h = w * (0.4f + rng.nextFloat() * 0.5f);
            const float x = r.getX() + rng.nextFloat() * r.getWidth() - w * 0.5f;
            const float y = r.getY() + rng.nextFloat() * r.getHeight() - h * 0.5f;
            juce::ColourGradient blob ((rng.nextBool() ? juce::Colours::black : oxide).withAlpha (0.06f),
                                       x + w * 0.5f, y + h * 0.5f, juce::Colours::transparentBlack, x + w, y + h * 0.5f, true);
            g.setGradientFill (blob);
            g.fillEllipse (x, y, w, h);
        }
        drawGrain (g, r, seed + 7, 0.05f, 0.05f);
    }

    // bevel
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.expanded (0.5f), corner + 0.5f, 1.0f);
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour (ink);
    g.fillRoundedRectangle (r, corner);

    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, corner);
    g.reduceClipRegion (clip);

    // inner shadow from the top
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.7f), r.getX(), r.getY(),
                                juce::Colours::transparentBlack, r.getX(), r.getY() + 14.0f, false);
    g.setGradientFill (shade);
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.translated (0.0f, 1.0f), corner, 1.0f);
}

juce::ColourGradient brassGradient (juce::Rectangle<float> r, float brightness)
{
    juce::ColourGradient cg (brassLight.withMultipliedBrightness (brightness), r.getX(), r.getY(),
                             brassDark.withMultipliedBrightness (brightness), r.getRight(), r.getBottom(), false);
    cg.addColour (0.45, brass.withMultipliedBrightness (brightness));
    cg.addColour (0.55, brass.darker (0.2f).withMultipliedBrightness (brightness));
    return cg;
}

void drawBrassTrim (juce::Graphics& g, juce::Rectangle<float> r, float corner, float thickness)
{
    g.setGradientFill (brassGradient (r, 0.9f));
    g.drawRoundedRectangle (r, corner, thickness);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (thickness * 0.5f + 0.5f), juce::jmax (0.0f, corner - thickness * 0.5f), 0.8f);
}

} // namespace oju::theme
