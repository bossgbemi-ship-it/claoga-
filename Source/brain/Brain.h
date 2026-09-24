#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include "Features.h"

namespace oju
{
struct BrainResult
{
    std::vector<std::pair<juce::String, float>> values;   // parameter id -> real (denormalised) value
    juce::StringArray read;                                // 4-6 plain-English lines
    juce::String idea;                                     // one suggestion for the artist
};

// Turns what OJU heard into settings. Pure and deterministic: no threads, no state.
BrainResult decide (const Features& features, int style, int mode, double bpm);

} // namespace oju
