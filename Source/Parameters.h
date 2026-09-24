#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace oju
{
namespace ids
{
    // Global
    inline constexpr const char* mode        = "mode";
    inline constexpr const char* style       = "style";
    inline constexpr const char* bypass      = "bypass";
    inline constexpr const char* inputGain   = "inputGain";
    inline constexpr const char* outputGain  = "outputGain";
    inline constexpr const char* amount      = "amount";
    inline constexpr const char* ceilingOn   = "ceilingOn";

    // Shape
    inline constexpr const char* lowCutOn     = "lowCutOn";
    inline constexpr const char* lowCutFreq   = "lowCutFreq";
    inline constexpr const char* eqOn         = "eqOn";
    inline constexpr const char* bodyGain     = "bodyGain";
    inline constexpr const char* mudFreq      = "mudFreq";
    inline constexpr const char* mudGain      = "mudGain";
    inline constexpr const char* presenceFreq = "presenceFreq";
    inline constexpr const char* presenceGain = "presenceGain";
    inline constexpr const char* airGain      = "airGain";

    // Tame
    inline constexpr const char* tameOn     = "tameOn";
    inline constexpr const char* tameFreq   = "tameFreq";
    inline constexpr const char* tameAmount = "tameAmount";

    // Press
    inline constexpr const char* pressOn        = "pressOn";
    inline constexpr const char* pressThreshold = "pressThreshold";
    inline constexpr const char* pressRatio     = "pressRatio";
    inline constexpr const char* pressAttack    = "pressAttack";
    inline constexpr const char* pressRelease   = "pressRelease";
    inline constexpr const char* pressMakeup    = "pressMakeup";
    inline constexpr const char* pressParallel  = "pressParallel";

    // Heat
    inline constexpr const char* heatOn    = "heatOn";
    inline constexpr const char* heatDrive = "heatDrive";
    inline constexpr const char* heatMix   = "heatMix";

    // Space
    inline constexpr const char* spaceOn       = "spaceOn";
    inline constexpr const char* delayTime     = "delayTime";
    inline constexpr const char* delayFeedback = "delayFeedback";
    inline constexpr const char* delayMix      = "delayMix";
    inline constexpr const char* reverbSize    = "reverbSize";
    inline constexpr const char* reverbMix     = "reverbMix";
}

enum class Mode  { natural = 0, extreme = 1 };
enum class Style { afrobeats = 0, trap, rnb, pop, soul };
enum class DelayDivision { slap = 0, sixteenth, eighth, dottedEighth, quarter };

juce::StringArray modeNames();
juce::StringArray styleNames();
juce::StringArray delayDivisionNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Every parameter the brain / A-B compare is allowed to write (everything except
// bypass, mode and style, which belong to the user).
const juce::StringArray& soundParameterIds();

} // namespace oju
