#pragma once

#include "Theme.h"

namespace oju
{
// Static chassis: cast iron, brushed steel, brass trim, rivets and engraved
// card titles. Buffered to an image (re-rendered at the current scale).
class Backplate : public juce::Component
{
public:
    Backplate();
    void paint (juce::Graphics&) override;
};

} // namespace oju
