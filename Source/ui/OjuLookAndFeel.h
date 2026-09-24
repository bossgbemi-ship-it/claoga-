#pragma once

#include "Theme.h"

namespace oju
{
class OjuLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OjuLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    juce::Font getPopupMenuFont() override { return theme::font (15.0f); }

    static void drawLamp (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool highlighted, float glow = 1.0f);
};

} // namespace oju
