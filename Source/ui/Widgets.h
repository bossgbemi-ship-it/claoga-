#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "OjuLookAndFeel.h"

namespace oju
{
//==============================================================================
// Big, touch-friendly rotary control with an engraved name and a live value.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name,
          bool bipolar = false, bool stepped = false);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider& getSlider() noexcept { return slider; }
    void setNote (const juce::String& n) { if (note != n) { note = n; repaint(); } }

private:
    juce::Slider slider;
    juce::String name, note;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
// Lamp switch bound to a bool parameter.
class LampSwitch : public juce::ToggleButton
{
public:
    LampSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label = {});

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
// Row of machined plates bound to a choice parameter (style, mode).
class SegmentedChoice : public juce::Component
{
public:
    SegmentedChoice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<float> p) const;

    juce::StringArray names;
    int selected = 0, hover = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedChoice)
};

//==============================================================================
// Engraved push plate. Lit state is drawn from getToggleState().
class PlateButton : public juce::Button
{
public:
    explicit PlateButton (const juce::String& caption) : juce::Button (caption) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    std::function<juce::String()> textProvider;   // optional dynamic caption
    juce::Colour litColour = theme::brass;
};

//==============================================================================
class Meter : public juce::Component
{
public:
    enum class Kind { level, reduction };

    Meter (Kind k, juce::String label) : kind (k), name (std::move (label)) {}

    void setValue (float linearOrDb);        // level: linear peak, reduction: dB
    void paint (juce::Graphics&) override;

private:
    Kind kind;
    juce::String name;
    float display = 0.0f, hold = 0.0f;
    int holdFrames = 0;
};

//==============================================================================
class ReadPanel : public juce::Component
{
public:
    void setContent (const juce::StringArray& lines, const juce::String& idea, const juce::String& tag, int mode);
    void paint (juce::Graphics&) override;

private:
    juce::StringArray lines;
    juce::String idea, tag;
    int mode = 0;
};

} // namespace oju
