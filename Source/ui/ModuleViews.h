#pragma once

#include "Rack.h"

namespace oju
{
// The panel above the rack: the selected module's controls, or The Read.
class DetailPanel : public juce::Component
{
public:
    static constexpr int readView = numModules;       // index of The Read
    static constexpr int beatView = numModules + 1;   // OJU Beat mode
    static constexpr int headerHeight = 50;

    DetailPanel();

    juce::Component& view (int index) { return *views[(size_t) index]; }
    int current() const noexcept { return shown; }
    void show (int index);

    // Header state for the shown module
    void setHeader (bool isOn, float grDb, const juce::String& extra);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (int)> onToggle;       // header lamp
    std::function<void()> onShowRead;

    juce::Rectangle<float> headerLamp() const;
    juce::Rectangle<float> readButton() const;
    juce::Rectangle<int> contentArea() const;

private:
    std::vector<std::unique_ptr<juce::Component>> views;
    int shown = readView;
    bool on = false;
    float gr = 0.0f;
    juce::String extra;
};

} // namespace oju
