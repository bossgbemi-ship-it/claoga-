#pragma once

#include "OjuLookAndFeel.h"

namespace oju
{
enum ModuleId { mDenoise = 0, mCleanup, mTune, mEq, mDeess, mCompress, mSaturate,
                mBreath, mDouble, mDelay, mReverb, mOutput, numModules };

struct ModuleInfo
{
    const char* name;
    const char* blurb;
    juce::StringArray toggles;    // parameters switched by the tile's lamp
    bool fromV1;                  // the original v1 module, untouched
    bool hasGr;                   // shows a gain-reduction meter
};

const ModuleInfo& moduleInfo (int id);

// One of the 12 big lit tiles in the rack.
class ModuleTile : public juce::Component
{
public:
    explicit ModuleTile (int moduleId);

    void setState (bool isOn, bool isSelected, const juce::String& statusText, float grDb);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hovered = false; repaint(); }

    std::function<void (int)> onSelect, onToggle;

    juce::Rectangle<float> lampArea() const;

private:
    int id;
    bool on = false, selected = false, hovered = false;
    juce::String status;
    float gr = 0.0f, grDisplay = 0.0f;
};

// Horizontal gain-reduction bar (0 .. 18 dB), used on tiles and module headers.
void drawGrBar (juce::Graphics& g, juce::Rectangle<float> r, float grDb, bool active);

} // namespace oju
