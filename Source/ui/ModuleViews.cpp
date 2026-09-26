#include "ModuleViews.h"

namespace oju
{
using namespace theme;

DetailPanel::DetailPanel()
{
    for (int i = 0; i < beatView + 1; ++i)
    {
        auto v = std::make_unique<juce::Component>();
        v->setInterceptsMouseClicks (false, true);
        addChildComponent (*v);
        views.push_back (std::move (v));
    }
    show (readView);
}

void DetailPanel::show (int index)
{
    shown = juce::jlimit (0, beatView, index);
    for (size_t i = 0; i < views.size(); ++i)
        views[i]->setVisible ((int) i == shown);
    repaint();
}

void DetailPanel::setHeader (bool isOn, float grDb, const juce::String& text)
{
    if (isOn == on && std::abs (grDb - gr) < 0.05f && text == extra)
        return;
    on = isOn; gr = grDb; extra = text;
    repaint (0, 0, getWidth(), headerHeight);
}

juce::Rectangle<int> DetailPanel::contentArea() const
{
    return getLocalBounds().withTrimmedTop (headerHeight).reduced (14, 6);
}

void DetailPanel::resized()
{
    for (size_t i = 0; i < views.size(); ++i)
        views[i]->setBounds ((int) i >= numModules ? getLocalBounds().reduced (6) : contentArea());
}

juce::Rectangle<float> DetailPanel::headerLamp() const
{
    return { 14.0f, 9.0f, 34.0f, 34.0f };
}

juce::Rectangle<float> DetailPanel::readButton() const
{
    return { (float) getWidth() - 118.0f, 10.0f, 104.0f, 32.0f };
}

void DetailPanel::mouseUp (const juce::MouseEvent& e)
{
    if (shown < numModules && headerLamp().expanded (6.0f).contains (e.position))
    {
        if (onToggle) onToggle (shown);
    }
    else if (shown != readView && shown != beatView && readButton().contains (e.position))
    {
        if (onShowRead) onShowRead();
    }
}

void DetailPanel::paint (juce::Graphics& g)
{
    if (shown >= numModules)
        return;   // The Read and Beat views draw their own titles

    const auto& info = moduleInfo (shown);
    auto header = getLocalBounds().toFloat().removeFromTop ((float) headerHeight);

    OjuLookAndFeel::drawLamp (g, headerLamp(), on, false, 1.0f);

    auto t = header.withTrimmedLeft (60.0f).withTrimmedRight (130.0f);
    const juce::String title = juce::String (shown + 1).paddedLeft ('0', 2) + "  " + info.name;
    drawEngravedText (g, title, t.removeFromTop (30.0f).translated (0.0f, 4.0f), juce::Justification::centredLeft,
                      font (21.0f, true), brassLight);
    g.setFont (font (13.5f));
    g.setColour (boneDim);
    g.drawText (juce::String (info.blurb) + (extra.isNotEmpty() ? juce::String::fromUTF8 ("  \xc2\xb7  ") + extra : juce::String()),
                t, juce::Justification::centredLeft, true);

    if (info.hasGr)
        drawGrBar (g, juce::Rectangle<float> (header.getRight() - 290.0f, 20.0f, 160.0f, 12.0f), gr, on);

    // back to The Read
    auto rb = readButton();
    drawRecess (g, rb, 5.0f);
    drawEngravedText (g, "The read", rb, juce::Justification::centred, font (14.5f, true), bone);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (headerHeight - 2, 12.0f, (float) getWidth() - 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine (headerHeight - 1, 12.0f, (float) getWidth() - 12.0f);
}

} // namespace oju
