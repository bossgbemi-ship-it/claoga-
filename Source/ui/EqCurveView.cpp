#include "EqCurveView.h"

namespace oju
{
using namespace theme;

namespace
{
    constexpr float minF = 20.0f, maxF = 20000.0f, rangeDb = 15.0f;

    juce::int64 hashOf (const ChainSettings& s, const Features& f)
    {
        const float vals[] = { s.lowCutOn ? 1.0f : 0.0f, s.lowCutFreq, s.eqOn ? 1.0f : 0.0f, s.bodyGain, s.mudFreq, s.mudGain,
                               s.presenceFreq, s.presenceGain, s.airGain, s.tameOn ? 1.0f : 0.0f, s.tameFreq, s.tameAmount,
                               f.valid ? 1.0f : 0.0f, f.spectrumDb[10], f.spectrumDb[40] };
        juce::int64 h = 1469598103934665603LL;
        for (auto v : vals)
            h = (h ^ (juce::int64) juce::roundToInt (v * 100.0f)) * 1099511628211LL;
        return h;
    }
}

float EqCurveView::xForFreq (float f, juce::Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * std::log (f / minF) / std::log (maxF / minF);
}

float EqCurveView::yForDb (float db, juce::Rectangle<float> r) const
{
    return r.getCentreY() - db / rangeDb * r.getHeight() * 0.5f;
}

void EqCurveView::update (const ChainSettings& s, double sampleRate, const Features& f)
{
    const auto h = hashOf (s, f);
    if (h == lastHash && juce::approximatelyEqual (sampleRate, rate))
        return;
    lastHash = h;
    settings = s;
    features = f;
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    repaint();
}

void EqCurveView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
    auto header = area.removeFromTop (24.0f);
    drawEngravedText (g, "Shape", header, juce::Justification::centredLeft, font (19.0f, true), brassLight);
    drawEngravedText (g, "what OJU set", header.withTrimmedLeft (64.0f), juce::Justification::centredLeft, font (14.5f), boneDim);

    area.removeFromTop (4.0f);
    auto labels = area.removeFromBottom (16.0f);
    auto r = area.withTrimmedLeft (30.0f);

    // grid
    const float freqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    const char* names[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
    g.setFont (font (12.5f));
    for (size_t i = 0; i < std::size (freqs); ++i)
    {
        const float x = xForFreq (freqs[i], r);
        g.setColour (bone.withAlpha (0.07f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        g.setColour (boneDim.withAlpha (0.8f));
        g.drawText (names[i], juce::Rectangle<float> (x - 20.0f, labels.getY(), 40.0f, labels.getHeight()), juce::Justification::centred);
    }
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = yForDb ((float) db, r);
        g.setColour (bone.withAlpha (db == 0 ? 0.16f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
        g.setColour (boneDim.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (area.getX(), y - 8.0f, 26.0f, 16.0f),
                    juce::Justification::centredRight);
    }

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());

    // voice spectrum from the last read
    if (features.valid)
    {
        juce::Path spec;
        const auto n = Features::spectrumPoints;
        spec.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < n; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1));
            const float db = juce::jlimit (-40.0f, 12.0f, features.spectrumDb[(size_t) i]);
            const float y = r.getBottom() - (db + 40.0f) / 52.0f * r.getHeight() * 0.85f;
            spec.lineTo (xForFreq (f, r), y);
        }
        spec.lineTo (r.getRight(), r.getBottom());
        spec.closeSubPath();
        g.setGradientFill (juce::ColourGradient (patina.withAlpha (0.28f), 0.0f, r.getY(), patina.withAlpha (0.04f), 0.0f, r.getBottom(), false));
        g.fillPath (spec);
    }

    // Tame split marker
    if (settings.tameOn)
    {
        const float x = xForFreq (settings.tameFreq, r);
        juce::Path dash;
        const float lengths[] = { 4.0f, 4.0f };
        juce::Path line;
        line.startNewSubPath (x, r.getY() + 4.0f);
        line.lineTo (x, r.getBottom());
        juce::PathStrokeType (1.0f).createDashedStroke (dash, line, lengths, 2);
        g.setColour (patina.brighter (0.2f).withAlpha (0.4f + 0.5f * settings.tameAmount));
        g.fillPath (dash);
        g.setFont (font (12.5f));
        g.drawText ("Tame", juce::Rectangle<float> (x + 4.0f, r.getY() + 2.0f, 50.0f, 14.0f), juce::Justification::centredLeft);
    }

    // EQ curve
    juce::Path curve, fill;
    const int steps = juce::roundToInt (r.getWidth() / 2.0f);
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
        const float f = minF * std::pow (maxF / minF, (float) i / (float) steps);
        const float db = shapeResponseDb (settings, f, rate);
        const float y = yForDb (juce::jlimit (-rangeDb - 6.0f, rangeDb + 6.0f, db), r);
        if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, yForDb (0.0f, r)); }
        else        curve.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (r.getRight(), yForDb (0.0f, r));
    fill.closeSubPath();

    g.setColour (brass.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (brass.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (brassLight);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // band handles
    auto handle = [&] (float f, juce::Colour col, bool show)
    {
        if (! show) return;
        const auto p = juce::Point<float> (xForFreq (f, r), yForDb (shapeResponseDb (settings, f, rate), r));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p));
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
    };
    handle (settings.lowCutFreq, boneDim, settings.lowCutOn);
    handle ((float) shape::bodyFreq, bone, settings.eqOn);
    handle (settings.mudFreq, oxide.brighter (0.3f), settings.eqOn);
    handle (settings.presenceFreq, brassLight, settings.eqOn);
    handle ((float) shape::airFreq, patina.brighter (0.3f), settings.eqOn);
}

} // namespace oju
