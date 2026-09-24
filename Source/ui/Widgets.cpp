#include "Widgets.h"

namespace oju
{
using namespace theme;

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n,
            bool bipolar, bool stepped)
    : name (n)
{
    param = state.getParameter (paramId);
    jassert (param != nullptr);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setMouseDragSensitivity (220);
    slider.setScrollWheelEnabled (true);
    slider.getProperties().set ("bipolar", bipolar);
    slider.getProperties().set ("stepped", stepped);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    setTitle (name);
    slider.setTitle (name);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (34);
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom (34.0f);
    const float a = isEnabled() ? 1.0f : 0.45f;
    drawEngravedText (g, name, text.removeFromTop (17.0f), juce::Justification::centred, font (14.5f), bone.withAlpha (a));

    const auto value = note.isNotEmpty() ? note : param->getCurrentValueAsText();
    g.setFont (font (13.5f));
    g.setColour ((note.isNotEmpty() ? patina : brassLight).withAlpha (0.9f * a));
    g.drawText (value, text, juce::Justification::centred, false);
}

//==============================================================================
LampSwitch::LampSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label)
    : juce::ToggleButton (label)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
    if (auto* p = state.getParameter (paramId))
        setTitle (p->getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

//==============================================================================
SegmentedChoice::SegmentedChoice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
    jassert (p != nullptr);
    names = p->choices;
    attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
    {
        selected = juce::roundToInt (v);
        repaint();
    });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (p->getName (64));
}

int SegmentedChoice::indexAt (juce::Point<float> p) const
{
    if (names.isEmpty() || ! getLocalBounds().toFloat().contains (p))
        return -1;
    return juce::jlimit (0, names.size() - 1, (int) (p.x / ((float) getWidth() / (float) names.size())));
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && i != selected)
        attachment->setValueAsCompleteGesture ((float) i);
}

void SegmentedChoice::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hover) { hover = i; repaint(); }
}

void SegmentedChoice::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawRecess (g, r, 6.0f);

    const float w = r.getWidth() / (float) names.size();
    for (int i = 0; i < names.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.0f);
        const bool on = i == selected;

        if (on)
        {
            g.setGradientFill (brassGradient (seg, 1.0f));
            g.fillRoundedRectangle (seg, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawHorizontalLine ((int) seg.getY() + 1, seg.getX() + 3.0f, seg.getRight() - 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (seg, 4.0f, 1.0f);
            drawEngravedText (g, names[i], seg, juce::Justification::centred, font (16.0f, true), soot, true);
        }
        else
        {
            if (i == hover)
            {
                g.setColour (bone.withAlpha (0.05f));
                g.fillRoundedRectangle (seg, 4.0f);
            }
            drawEngravedText (g, names[i], seg, juce::Justification::centred, font (16.0f), i == hover ? bone : boneDim);
        }

        if (i > 0 && ! on && i - 1 != selected)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawVerticalLine ((int) seg.getX() - 3, seg.getY() + 6.0f, seg.getBottom() - 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) seg.getX() - 2, seg.getY() + 6.0f, seg.getBottom() - 6.0f);
        }
    }
}

//==============================================================================
void PlateButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (down)
        r = r.translated (0.0f, 1.0f);

    const bool lit = getToggleState();
    drawRecess (g, r, 6.0f);
    auto face = r.reduced (3.0f);

    juce::ColourGradient body (lit ? litColour.brighter (0.25f) : ironLight.brighter (highlighted ? 0.15f : 0.0f), face.getX(), face.getY(),
                               lit ? litColour.darker (0.5f) : iron.darker (0.3f), face.getX(), face.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (face, 4.0f);
    drawBrushed (g, face.reduced (2.0f), 11, 0.05f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine ((int) face.getY() + 1, face.getX() + 3.0f, face.getRight() - 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (face, 4.0f, 1.0f);

    const auto caption = textProvider ? textProvider() : getButtonText();
    drawEngravedText (g, caption, face, juce::Justification::centred, font (16.0f, lit), lit ? soot : bone, lit);
}

//==============================================================================
void Meter::setValue (float v)
{
    float target = 0.0f;
    if (kind == Kind::level)
    {
        const float db = juce::Decibels::gainToDecibels (v, -70.0f);
        target = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }
    else
    {
        target = juce::jlimit (0.0f, 1.0f, v / 20.0f);
    }

    display = target > display ? target : display + (target - display) * 0.18f;
    if (display >= hold) { hold = display; holdFrames = 45; }
    else if (--holdFrames < 0) hold = juce::jmax (display, hold - 0.012f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto label = r.removeFromBottom (22.0f);
    drawEngravedText (g, name, label, juce::Justification::centred, font (14.0f), bone);

    auto well = r.withSizeKeepingCentre (juce::jmin (26.0f, r.getWidth()), r.getHeight());
    drawRecess (g, well, 4.0f);
    auto bar = well.reduced (4.0f);

    const int segments = 30;
    const float gap = 2.0f;
    const float segH = (bar.getHeight() - gap * (float) (segments - 1)) / (float) segments;

    for (int i = 0; i < segments; ++i)
    {
        const float frac = (float) i / (float) segments;           // 0 = bottom (level) / top (reduction)
        const float y = kind == Kind::level ? bar.getBottom() - (float) (i + 1) * segH - (float) i * gap
                                            : bar.getY() + (float) i * (segH + gap);
        const auto seg = juce::Rectangle<float> (bar.getX(), y, bar.getWidth(), segH);
        const bool lit = frac < display;

        juce::Colour c;
        if (kind == Kind::reduction)   c = oxide.brighter (0.2f);
        else if (frac > 0.9f)          c = oxide.brighter (0.35f);
        else if (frac > 0.75f)         c = brassLight;
        else                           c = patina.brighter (0.2f);

        g.setColour (lit ? c : c.withAlpha (0.08f));
        g.fillRoundedRectangle (seg, 1.0f);
    }

    if (hold > 0.02f)
    {
        const float hy = kind == Kind::level ? bar.getBottom() - hold * bar.getHeight() : bar.getY() + hold * bar.getHeight();
        g.setColour (bone.withAlpha (0.85f));
        g.fillRect (bar.getX(), hy - 1.0f, bar.getWidth(), 2.0f);
    }
}

//==============================================================================
void ReadPanel::setContent (const juce::StringArray& l, const juce::String& i, const juce::String& t, int m)
{
    if (l == lines && i == idea && t == tag && m == mode)
        return;
    lines = l; idea = i; tag = t; mode = m;
    repaint();
}

void ReadPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (18.0f, 12.0f);
    auto header = r.removeFromTop (26.0f);
    drawEngravedText (g, "The read", header, juce::Justification::centredLeft, font (19.0f, true), brassLight);
    if (tag.isNotEmpty())
        drawEngravedText (g, tag, header, juce::Justification::centredRight, font (14.5f), boneDim);
    r.removeFromTop (6.0f);

    if (lines.isEmpty())
    {
        g.setFont (font (16.0f));
        g.setColour (boneDim);
        g.drawFittedText (juce::String::fromUTF8 ("Press the eye and sing a few lines. OJU only counts the moments you are "
                                                  "actually singing \xe2\x80\x94 about 4 seconds in Natural, 10 in Extreme \xe2\x80\x94 "
                                                  "then sets every module for you and tells you what it heard."),
                          r.toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
        return;
    }

    const float lineH = juce::jmin (22.0f, r.getHeight() / ((float) lines.size() + 1.3f));
    for (const auto& l : lines)
    {
        auto row = r.removeFromTop (lineH);
        auto bullet = row.removeFromLeft (16.0f);
        juce::Path d;
        const auto bc = bullet.getCentre().translated (-2.0f, 0.0f);
        d.addTriangle (bc.x - 4.0f, bc.y, bc.x, bc.y - 4.0f, bc.x + 4.0f, bc.y);
        d.addTriangle (bc.x - 4.0f, bc.y, bc.x, bc.y + 4.0f, bc.x + 4.0f, bc.y);
        g.setColour (brass);
        g.fillPath (d);
        g.setFont (font (16.5f));
        g.setColour (bone);
        g.drawText (l, row, juce::Justification::centredLeft, true);
    }

    if (idea.isNotEmpty())
    {
        r.removeFromTop (4.0f);
        auto row = r.removeFromTop (lineH);
        g.setColour (patina.withAlpha (0.35f));
        g.drawHorizontalLine ((int) row.getY() - 2, row.getX(), row.getX() + 60.0f);
        g.setFont (font (16.5f, true));
        g.setColour (patina.brighter (0.25f));
        g.drawText ("Idea: " + idea, row, juce::Justification::centredLeft, true);
    }
}

} // namespace oju
