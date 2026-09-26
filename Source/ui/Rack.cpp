#include "Rack.h"
#include "../Parameters.h"

namespace oju
{
using namespace theme;

const ModuleInfo& moduleInfo (int id)
{
    static const ModuleInfo infos[numModules] = {
        { "Denoise",   "RNNoise cleanup, plus Learn Room",          { ids::denoiseOn },                 false, false },
        { "Cleanup",   "High-pass by voice type, plosive tamer",    { ids::lowCutOn, ids::plosiveOn },  false, false },
        { "Tune",      "Pitch correction with formants kept",       { ids::tuneOn },                    false, false },
        { "EQ",        "The OJU v1 Shape EQ, unchanged",            { ids::eqOn },                      true,  false },
        { "De-esser",  "Split-band Tame, auto sibilance band",      { ids::tameOn },                    true,  true  },
        { "Compress",  "Leveler, then the v1 Press",                { ids::levelOn, ids::pressOn },     true,  true  },
        { "Saturate",  "The OJU v1 Heat, unchanged",                { ids::heatOn },                    true,  false },
        { "Breath",    "Turns breaths down, never deletes them",    { ids::breathOn },                  false, false },
        { "Double",    "Synthetic double and stereo width",         { ids::doubleOn },                  false, false },
        { "Delay",     "Tempo-synced, filtered, ducks while singing", { ids::echoOn },                  true,  false },
        { "Reverb",    "Room, plate, hall; blooms in the gaps",     { ids::verbOn },                    true,  false },
        { "Output",    "Vocal rider and -1 dB safety limiter",      { ids::riderOn, ids::limiterOn },   false, true  },
    };
    return infos[juce::jlimit (0, numModules - 1, id)];
}

void drawGrBar (juce::Graphics& g, juce::Rectangle<float> r, float grDb, bool active)
{
    drawRecess (g, r, 3.0f);
    auto inner = r.reduced (2.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, grDb / 18.0f);
    if (active && frac > 0.001f)
    {
        g.setColour (oxide.brighter (0.25f));
        g.fillRoundedRectangle (inner.withWidth (inner.getWidth() * frac), 2.0f);
    }
    g.setColour (bone.withAlpha (0.15f));
    for (int db : { 3, 6, 12 })
        g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) db / 18.0f), inner.getY(), inner.getBottom());
}

ModuleTile::ModuleTile (int moduleId) : id (moduleId)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (moduleInfo (id).name);
    setDescription (moduleInfo (id).blurb);
}

void ModuleTile::setState (bool isOn, bool isSelected, const juce::String& statusText, float grDb)
{
    grDisplay = grDb > grDisplay ? grDb : grDisplay + (grDb - grDisplay) * 0.2f;
    if (isOn == on && isSelected == selected && statusText == status && std::abs (grDisplay - gr) < 0.05f)
        return;
    on = isOn; selected = isSelected; status = statusText; gr = grDisplay;
    repaint();
}

juce::Rectangle<float> ModuleTile::lampArea() const
{
    auto r = getLocalBounds().toFloat();
    return juce::Rectangle<float> (50.0f, 50.0f).withPosition (r.getRight() - 62.0f, r.getBottom() - 62.0f);
}

void ModuleTile::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    if (lampArea().expanded (8.0f).contains (e.position))
    {
        if (onToggle) onToggle (id);
    }
    else if (onSelect)
    {
        onSelect (id);
    }
}

void ModuleTile::paint (juce::Graphics& g)
{
    const auto& info = moduleInfo (id);
    auto r = getLocalBounds().toFloat().reduced (1.5f);

    drawIronPlate (g, r, 9.0f, 300 + id);
    if (hovered)
    {
        g.setColour (bone.withAlpha (0.035f));
        g.fillRoundedRectangle (r, 9.0f);
    }
    if (selected)
    {
        g.setColour (brass.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 6.0f);
        drawBrassTrim (g, r.reduced (2.5f), 8.0f, 1.6f);
    }

    // number plate + name
    auto text = r.reduced (14.0f, 12.0f);
    auto top = text.removeFromTop (30.0f);
    {
        auto num = top.removeFromLeft (30.0f).withSizeKeepingCentre (28.0f, 28.0f);
        g.setGradientFill (brassGradient (num, on ? 1.0f : 0.6f));
        g.fillEllipse (num);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (num, 1.0f);
        drawEngravedText (g, juce::String (id + 1), num, juce::Justification::centred, font (15.0f, true), soot, true);
    }
    top.removeFromLeft (10.0f);
    drawEngravedText (g, info.name, top, juce::Justification::centredLeft, font (22.0f, true), on ? bone : boneDim);
    if (info.fromV1)
        drawEngravedText (g, "v1", top, juce::Justification::centredRight, font (12.5f, true), brass.withAlpha (0.75f));

    text.removeFromTop (6.0f);
    g.setFont (font (13.5f));
    g.setColour (boneDim.withAlpha (on ? 0.95f : 0.6f));
    g.drawFittedText (info.blurb, text.removeFromTop (34.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);

    // status + gain reduction, left of the lamp
    auto lower = text.withTrimmedRight (60.0f);
    auto bottom = lower.removeFromBottom (22.0f);
    if (info.hasGr)
    {
        auto bar = lower.removeFromBottom (14.0f).reduced (0.0f, 1.0f);
        drawGrBar (g, bar, gr, on);
    }
    g.setFont (font (14.5f, true));
    g.setColour ((on ? brassLight : boneDim).withAlpha (on ? 1.0f : 0.55f));
    g.drawFittedText (on ? status : juce::String ("Off"), bottom.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    // the big lamp switch
    OjuLookAndFeel::drawLamp (g, lampArea().reduced (4.0f), on, hovered, 1.2f);
}

} // namespace oju
