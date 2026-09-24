#pragma once

#include "../PluginProcessor.h"
#include "OjuLookAndFeel.h"
#include "Widgets.h"
#include "EyeButton.h"
#include "EqCurveView.h"
#include "Backplate.h"

namespace oju
{
class OjuEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit OjuEditor (OjuProcessor&);
    ~OjuEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void eyeClicked();
    bool standaloneInputMuted() const;
    void unmuteStandaloneInput();
    Knob& addKnob (const char* id, const juce::String& name, int card, int col, int row, bool bipolar = false, bool stepped = false);
    LampSwitch& addSwitch (const char* id, juce::Rectangle<int> bounds, const juce::String& label = {});

    // Painted text under the eye.
    struct EyeStatus : public juce::Component
    {
        juce::String title, subtitle;
        void set (const juce::String& t, const juce::String& s) { if (t != title || s != subtitle) { title = t; subtitle = s; repaint(); } }
        void paint (juce::Graphics&) override;
    };

    struct Readout : public juce::Component
    {
        juce::String name, value;
        void set (const juce::String& v) { if (v != value) { value = v; repaint(); } }
        void paint (juce::Graphics&) override;
    };

    OjuProcessor& ojuProcessor;
    OjuLookAndFeel lookAndFeel;

    juce::Component content;
    Backplate backplate;

    SegmentedChoice styleChoice, modeChoice;
    PlateButton abButton { "A/B" }, bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    EyeButton eye;
    EyeStatus eyeStatus;
    EqCurveView eqCurve;
    ReadPanel readPanel;
    Meter inMeter { Meter::Kind::level, "In" }, grMeter { Meter::Kind::reduction, "Press" }, outMeter { Meter::Kind::level, "Out" };
    Readout tempo, tameLamp;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<LampSwitch>> switches;
    Knob* parallelKnob = nullptr;

    juce::TooltipWindow tooltips { this, 700 };

    int stalledFrames = 0, unmuteArmedFrames = 0;
    float lastProgress = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OjuEditor)
};

} // namespace oju
