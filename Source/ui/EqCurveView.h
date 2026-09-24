#pragma once

#include "Theme.h"
#include "../dsp/VocalChain.h"
#include "../brain/Features.h"

namespace oju
{
// Live curve of what OJU set in Shape (low cut + EQ), with Tame's split point
// and — after a read — the voice's own spectrum behind it.
class EqCurveView : public juce::Component
{
public:
    void update (const ChainSettings& s, double sampleRate, const Features& f);
    void paint (juce::Graphics&) override;

private:
    float xForFreq (float f, juce::Rectangle<float> r) const;
    float yForDb (float db, juce::Rectangle<float> r) const;

    ChainSettings settings;
    Features features;
    double rate = 48000.0;
    juce::int64 lastHash = 0;
};

} // namespace oju
