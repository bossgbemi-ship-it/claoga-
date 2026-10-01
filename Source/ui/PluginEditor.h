#pragma once

#include "../PluginProcessor.h"
#include "OjuLookAndFeel.h"
#include "Widgets.h"
#include "EyeButton.h"
#include "EqCurveView.h"
#include "Backplate.h"
#include "Rack.h"
#include "ModuleViews.h"

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

    void showModule (int id) { selectModule (id); }   // also used by the test runner

private:
    void timerCallback() override;
    void eyeClicked();
    bool standaloneInputMuted() const;
    void unmuteStandaloneInput();

    // Rack
    bool moduleOn (int id) const;
    void toggleModule (int id);
    void selectModule (int id);
    juce::String tileStatus (int id) const;
    juce::String tempoText() const;
    float tileGr (int id) const;

    // Building the module views
    void buildViews();
    Knob& addKnob (juce::Component& parent, const char* id, const juce::String& name, int col, int row,
                   bool bipolar = false, bool stepped = false);
    LampSwitch& addSwitch (juce::Component& parent, const char* id, const juce::String& label, juce::Rectangle<int> bounds);
    static juce::Rectangle<int> cell (int col, int row, int colSpan = 1, int x0 = 0);

    void showPresetsMenu();

    // Painted text under the eye.
    struct EyeStatus : public juce::Component
    {
        juce::String title, subtitle;
        void set (const juce::String& t, const juce::String& s) { if (t != title || s != subtitle) { title = t; subtitle = s; repaint(); } }
        void paint (juce::Graphics&) override;
    };

    // A small engraved readout plate: a title line and a value line.
    struct Readout : public juce::Component
    {
        juce::String name, value;
        juce::Colour valueColour = theme::brassLight;
        void set (const juce::String& v) { if (v != value) { value = v; repaint(); } }
        void paint (juce::Graphics&) override;
    };

    // Tempo, CPU + latency, and Beat Link status in the right-hand column.
    struct StatusInfo : public juce::Component
    {
        juce::String tempo, load, link;
        bool tempoLive = false, linked = false;
        void set (const juce::String& t, bool isLive, const juce::String& l, const juce::String& k, bool isLinked)
        {
            if (t == tempo && isLive == tempoLive && l == load && k == link && isLinked == linked) return;
            tempo = t; tempoLive = isLive; load = l; link = k; linked = isLinked; repaint();
        }
        void paint (juce::Graphics&) override;
    };

    // Fills the rack area in OJU Beat mode: the key, big, and how to link.
    struct BeatGuide : public juce::Component
    {
        juce::String key, detail;
        void set (const juce::String& k, const juce::String& d) { if (k != key || d != detail) { key = k; detail = d; repaint(); } }
        void paint (juce::Graphics&) override;
    };

    struct MeterFrame
    {
        float in = 0, out = 0, press = 0, tame = 0, level = 0, limiter = 0, plosive = 0;
    };

    OjuProcessor& ojuProcessor;
    OjuLookAndFeel lookAndFeel;

    juce::Component content;
    Backplate backplate;

    SegmentedChoice roleChoice, genreChoice, modeChoice, latencyChoice;
    PlateButton presetsButton { "Presets" }, abButton { "A/B" }, v1Button { "v1" }, bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    EyeButton eye;
    EyeStatus eyeStatus;
    DetailPanel detail;
    ReadPanel readPanel;
    EqCurveView eqCurve;
    Meter inMeter { Meter::Kind::level, "In" }, grMeter { Meter::Kind::reduction, "Comp" }, outMeter { Meter::Kind::level, "Out" };
    StatusInfo statusInfo;
    BeatGuide beatGuide;

    std::array<std::unique_ptr<ModuleTile>, numModules> tiles;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<LampSwitch>> switches;
    std::vector<std::unique_ptr<juce::Component>> extras;

    Knob* parallelKnob = nullptr;
    Readout* denoiseInfo = nullptr;
    Readout* cleanupInfo = nullptr;
    Readout* tuneInfo = nullptr;
    Readout* deessInfo = nullptr;
    Readout* tempoInfo = nullptr;
    Readout* outputInfo = nullptr;
    Readout* beatInfo = nullptr;
    PlateButton* learnRoomButton = nullptr;
    PlateButton* loadBeatButton = nullptr;

    MeterFrame meters;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    int stalledFrames = 0, unmuteArmedFrames = 0, lastReadVersion = -1;
    float lastProgress = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OjuEditor)
};

} // namespace oju
