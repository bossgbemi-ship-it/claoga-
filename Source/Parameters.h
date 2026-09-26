#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

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

    //==========================================================================
    // OJU 2.0 (all default to v1 behaviour: new modules off)
    inline constexpr const char* role          = "role";          // Vocal / Beat
    inline constexpr const char* linkGroup     = "linkGroup";
    inline constexpr const char* latencyMode   = "latencyMode";   // Mix / Track

    inline constexpr const char* denoiseOn     = "denoiseOn";
    inline constexpr const char* denoiseAmount = "denoiseAmount";
    inline constexpr const char* roomAmount    = "roomAmount";

    inline constexpr const char* plosiveOn     = "plosiveOn";
    inline constexpr const char* plosiveAmount = "plosiveAmount";

    inline constexpr const char* tuneOn        = "tuneOn";
    inline constexpr const char* tuneKeySource = "tuneKeySource"; // Auto / Manual
    inline constexpr const char* tuneKey       = "tuneKey";
    inline constexpr const char* tuneScale     = "tuneScale";
    inline constexpr const char* tuneSpeed     = "tuneSpeed";
    inline constexpr const char* tuneHumanize  = "tuneHumanize";
    inline constexpr const char* tuneMix       = "tuneMix";

    inline constexpr const char* deessAuto     = "deessAuto";

    inline constexpr const char* levelOn       = "levelOn";
    inline constexpr const char* levelAmount   = "levelAmount";

    inline constexpr const char* breathOn      = "breathOn";
    inline constexpr const char* breathAmount  = "breathAmount";

    inline constexpr const char* doubleOn      = "doubleOn";
    inline constexpr const char* doubleAmount  = "doubleAmount";
    inline constexpr const char* width         = "width";
    inline constexpr const char* hookOnly      = "hookOnly";

    inline constexpr const char* echoOn        = "echoOn";
    inline constexpr const char* echoDuck      = "echoDuck";
    inline constexpr const char* verbOn        = "verbOn";
    inline constexpr const char* verbType      = "verbType";
    inline constexpr const char* verbDuck      = "verbDuck";

    inline constexpr const char* riderOn       = "riderOn";
    inline constexpr const char* riderAmount   = "riderAmount";
    inline constexpr const char* limiterOn     = "limiterOn";
    inline constexpr const char* limiterCeiling = "limiterCeiling";

    inline constexpr const char* carveOn       = "carveOn";
    inline constexpr const char* carveDepth    = "carveDepth";
}

enum class Mode  { natural = 0, extreme = 1 };
// v1 styles keep their indices; OJU 2.0 genres are appended so v1 presets load unchanged.
enum class Style { afrobeats = 0, trap, rnb, pop, soul, rap, amapiano, gospel };
enum class Role { vocal = 0, beat };
enum class LatencyMode { mix = 0, track };
enum class Scale { major = 0, minor, chromatic, majorPentatonic, minorPentatonic, harmonicMinor };
enum class VerbType { classic = 0, room, plate, hall };
enum class DelayDivision { slap = 0, sixteenth, eighth, dottedEighth, quarter };

juce::StringArray modeNames();
juce::StringArray styleNames();
juce::StringArray delayDivisionNames();
juce::StringArray keyNames();
juce::StringArray scaleNames();
juce::StringArray verbTypeNames();

// The five genres shown in OJU 2.0 (indices into styleNames()).
const std::array<int, 5>& genreStyles();

// Parameters that only exist in OJU 2.0 (reset to defaults when a v1 preset loads).
const juce::StringArray& v2ParameterIds();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Every parameter the brain / A-B compare is allowed to write (everything except
// bypass, mode and style, which belong to the user).
const juce::StringArray& soundParameterIds();

} // namespace oju
