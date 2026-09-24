#include "EyeButton.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace oju
{
using namespace theme;

EyeButton::EyeButton()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Listen");
    setDescription ("Press to let OJU listen to the vocal and set itself up");
}

bool EyeButton::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = (float) juce::jmin (getWidth(), getHeight()) * 0.5f;
    return c.getDistanceFrom ({ (float) x, (float) y }) <= r;
}

void EyeButton::mouseUp (const juce::MouseEvent& e)
{
    pressed = false;
    repaint();
    if (hitTest (e.x, e.y) && onClick)
        onClick();
}

void EyeButton::update (Phase p, float prog, float inputLevel)
{
    if (p == Phase::done && phase != Phase::done)
        doneFlash = 1.0f;

    phase = p;
    progress = prog;

    // perceptual level 0..1 from linear peak
    const float db = juce::Decibels::gainToDecibels (inputLevel, -60.0f);
    const float target = juce::jlimit (0.0f, 1.0f, (db + 50.0f) / 50.0f);
    level += (target - level) * (target > level ? 0.5f : 0.12f);

    const float pupilTarget = phase == Phase::listening ? 0.55f + 0.45f * level : (phase == Phase::analysing ? 0.3f : 0.35f + 0.25f * level);
    pupil += (pupilTarget - pupil) * 0.25f;

    if (phase == Phase::analysing || phase == Phase::listening)
        spin += 0.05f;
    doneFlash = juce::jmax (0.0f, doneFlash - 0.02f);
    repaint();
}

void EyeButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    const float squash = pressed ? 0.985f : 1.0f;
    const float twoPi = juce::MathConstants<float>::twoPi;
    auto circle = [c] (float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c); };

    // ---- cast-iron bezel
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (circle (R).translated (0.0f, 4.0f));
    juce::ColourGradient bez (ironLight.brighter (0.15f), c.x - R * 0.6f, c.y - R * 0.8f, ink, c.x + R * 0.5f, c.y + R, true);
    g.setGradientFill (bez);
    g.fillEllipse (circle (R));
    drawGrain (g, circle (R).reduced (R * 0.05f), 1234, 0.02f, 0.06f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (circle (R - 1.0f), 1.0f);

    for (int i = 0; i < 8; ++i)
        drawRivet (g, c.getPointOnCircumference (R * 0.915f, twoPi * ((float) i + 0.5f) / 8.0f), R * 0.03f);

    // ---- progress track
    const float ringR = R * 0.82f;
    const float ringW = R * 0.045f;
    g.setColour (ink);
    g.drawEllipse (circle (ringR), ringW + 3.0f);

    // engraved minute ticks on the track
    for (int i = 0; i < 60; ++i)
    {
        const float a = twoPi * (float) i / 60.0f;
        const float len = (i % 5 == 0) ? ringW * 0.9f : ringW * 0.45f;
        g.setColour (bone.withAlpha (i % 5 == 0 ? 0.22f : 0.1f));
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (ringR - len * 0.5f, a), c.getPointOnCircumference (ringR + len * 0.5f, a)), 1.0f);
    }

    const bool active = phase == Phase::listening || phase == Phase::analysing;
    if (phase == Phase::listening && progress > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, 0.0f, twoPi * progress, true);
        g.setColour (brassLight.withAlpha (0.25f));
        g.strokePath (arc, juce::PathStrokeType (ringW * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (brassLight);
        g.strokePath (arc, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (phase == Phase::analysing)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, spin * 3.0f, spin * 3.0f + 1.4f, true);
        g.setColour (patina.brighter (0.3f));
        g.strokePath (arc, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (phase == Phase::done)
    {
        g.setColour (patina.withAlpha (0.35f + 0.5f * doneFlash));
        g.drawEllipse (circle (ringR), ringW * 0.6f);
    }

    // ---- level halo behind the brass ring
    const float brassR = R * 0.72f * squash;
    if (level > 0.01f)
    {
        juce::ColourGradient halo (brassLight.withAlpha ((active ? 0.45f : 0.2f) * level), c.x, c.y,
                                   juce::Colours::transparentBlack, c.x + brassR * 1.12f, c.y, true);
        halo.addColour (0.8, brass.withAlpha ((active ? 0.25f : 0.1f) * level));
        g.setGradientFill (halo);
        g.fillEllipse (circle (brassR * 1.12f));
    }

    // ---- machined brass ring (knurled)
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (circle (brassR + 2.0f).translated (0.0f, 2.0f));
    g.setGradientFill (brassGradient (circle (brassR), hovered ? 1.12f : 1.0f));
    g.fillEllipse (circle (brassR));

    for (int i = 0; i < 120; ++i)
    {
        const float a = twoPi * (float) i / 120.0f;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (brassR * 0.93f, a), c.getPointOnCircumference (brassR, a)), 1.0f);
    }
    // turned grooves
    for (int i = 0; i < 5; ++i)
    {
        const float rr = brassR * (0.78f + 0.03f * (float) i);
        g.setColour ((i % 2 ? juce::Colours::white : juce::Colours::black).withAlpha (0.12f));
        g.drawEllipse (circle (rr), 0.8f);
    }

    // ---- recess to the iris
    const float recessR = brassR * 0.76f;
    juce::ColourGradient recess (ink, c.x, c.y - recessR, soot.brighter (0.1f), c.x, c.y + recessR, false);
    g.setGradientFill (recess);
    g.fillEllipse (circle (recessR));

    // ---- iris: concentric machined rings in brass, oxide and patina
    const float irisR = recessR * 0.9f;
    juce::ColourGradient iris (brassLight, c.x, c.y, brassDark.darker (0.3f), c.x + irisR, c.y, true);
    iris.addColour (0.35, brass);
    iris.addColour (0.62, oxide);
    iris.addColour (0.8, patina.darker (0.35f));
    g.setGradientFill (iris);
    g.fillEllipse (circle (irisR));

    for (int i = 0; i < 9; ++i)
    {
        const float rr = irisR * (0.32f + 0.075f * (float) i);
        g.setColour ((i % 2 ? juce::Colours::black : brassLight).withAlpha (i % 2 ? 0.35f : 0.18f));
        g.drawEllipse (circle (rr), i % 3 == 0 ? 1.6f : 0.8f);
    }

    // radial striations
    juce::Random rng (77);
    for (int i = 0; i < 96; ++i)
    {
        const float a = twoPi * (float) i / 96.0f + rng.nextFloat() * 0.03f;
        const float r0 = irisR * (0.3f + 0.05f * rng.nextFloat());
        const float r1 = irisR * (0.75f + 0.22f * rng.nextFloat());
        g.setColour ((i % 3 == 0 ? brassLight : juce::Colours::black).withAlpha (i % 3 == 0 ? 0.14f : 0.2f));
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (r0, a), c.getPointOnCircumference (r1, a)), 0.7f);
    }

    // ---- pupil
    const float pupilR = irisR * (0.24f + 0.16f * pupil);
    juce::ColourGradient pup (juce::Colour (0xff050404), c.x, c.y, ink.brighter (0.05f), c.x + pupilR, c.y, true);
    g.setGradientFill (pup);
    g.fillEllipse (circle (pupilR));
    g.setColour ((active ? patina.brighter (0.4f) : brassDark).withAlpha (active ? 0.8f : 0.6f));
    g.drawEllipse (circle (pupilR), active ? 2.0f : 1.2f);

    if (active)
    {
        juce::ColourGradient inner (patina.withAlpha (0.25f * (0.4f + level)), c.x, c.y,
                                    juce::Colours::transparentBlack, c.x + pupilR, c.y, true);
        g.setGradientFill (inner);
        g.fillEllipse (circle (pupilR));
    }

    // ---- glass reflections
    juce::Path glint;
    glint.addCentredArc (c.x, c.y, recessR * 0.82f, recessR * 0.82f, 0.0f, -2.2f, -1.0f, true);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.strokePath (glint, juce::PathStrokeType (recessR * 0.06f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillEllipse (juce::Rectangle<float> (pupilR * 0.5f, pupilR * 0.38f).withCentre (c.translated (-pupilR * 0.9f, -pupilR * 1.1f)));
}

} // namespace oju
