#include "Parameters.h"

namespace oju
{
juce::StringArray modeNames()          { return { "Natural", "Extreme" }; }
juce::StringArray styleNames()         { return { "Afrobeats", "Trap", "R&B", "Pop", "Soul", "Rap", "Amapiano", "Gospel" }; }
juce::StringArray keyNames()           { return { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; }
juce::StringArray scaleNames()         { return { "Major", "Minor", "Chromatic", "Major pent.", "Minor pent.", "Harmonic minor" }; }
juce::StringArray verbTypeNames()      { return { "Classic", "Room", "Plate", "Hall" }; }

const std::array<int, 5>& genreStyles()
{
    static const std::array<int, 5> g { (int) Style::afrobeats, (int) Style::rap, (int) Style::rnb,
                                        (int) Style::amapiano, (int) Style::gospel };
    return g;
}
juce::StringArray delayDivisionNames() { return { "Slap", "1/16", "1/8", "1/8 dotted", "1/4" }; }

namespace
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;

    constexpr int version = 1;
    constexpr int version2 = 2;

    juce::ParameterID pid (const char* id) { return { id, version }; }
    juce::ParameterID pid2 (const char* id) { return { id, version2 }; }

    juce::String formatDb (float v, int)
    {
        const auto rounded = std::round (v * 10.0f) / 10.0f;
        return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
    }

    juce::String formatHz (float v, int)
    {
        if (v >= 1000.0f)
            return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String formatMs (float v, int)      { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String formatRatio (float v, int)   { return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, interval);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<APF> dbParam (const char* id, const juce::String& name, float lo, float hi, float def)
    {
        return std::make_unique<APF> (pid (id), name, juce::NormalisableRange<float> (lo, hi, 0.01f), def,
                                      Attr().withLabel ("dB").withStringFromValueFunction (formatDb));
    }

    std::unique_ptr<APF> hzParam (const char* id, const juce::String& name, float lo, float hi, float centre, float def)
    {
        return std::make_unique<APF> (pid (id), name, skewed (lo, hi, centre, 0.1f), def,
                                      Attr().withLabel ("Hz").withStringFromValueFunction (formatHz));
    }

    std::unique_ptr<APF> pctParam (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<APF> (pid (id), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                                      Attr().withLabel ("%").withStringFromValueFunction (formatPercent));
    }

    std::unique_ptr<APB> onParam (const char* id, const juce::String& name, bool def = true)
    {
        return std::make_unique<APB> (pid (id), name, def);
    }

    std::unique_ptr<APF> pct2 (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<APF> (pid2 (id), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                                      Attr().withLabel ("%").withStringFromValueFunction (formatPercent));
    }

    std::unique_ptr<APB> on2 (const char* id, const juce::String& name, bool def = false)
    {
        return std::make_unique<APB> (pid2 (id), name, def);
    }

    std::unique_ptr<APC> choice2 (const char* id, const juce::String& name, const juce::StringArray& c, int def)
    {
        return std::make_unique<APC> (pid2 (id), name, c, def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Global
    layout.add (std::make_unique<APC> (pid (ids::mode), "Mode", modeNames(), 0));
    layout.add (std::make_unique<APC> (pid (ids::style), "Style", styleNames(), 0));
    layout.add (onParam (ids::bypass, "Bypass", false));
    layout.add (dbParam (ids::inputGain, "Input", -24.0f, 24.0f, 0.0f));

    // Shape
    layout.add (onParam (ids::lowCutOn, "Low cut on"));
    layout.add (hzParam (ids::lowCutFreq, "Low cut", 20.0f, 300.0f, 90.0f, 80.0f));
    layout.add (onParam (ids::eqOn, "Shape on"));
    layout.add (dbParam (ids::bodyGain, "Body", -9.0f, 9.0f, 0.0f));
    layout.add (hzParam (ids::mudFreq, "Mud freq", 120.0f, 1000.0f, 350.0f, 300.0f));
    layout.add (dbParam (ids::mudGain, "Mud", -12.0f, 3.0f, 0.0f));
    layout.add (hzParam (ids::presenceFreq, "Presence freq", 1500.0f, 6000.0f, 3200.0f, 3500.0f));
    layout.add (dbParam (ids::presenceGain, "Presence", -6.0f, 9.0f, 0.0f));
    layout.add (dbParam (ids::airGain, "Air", -6.0f, 10.0f, 0.0f));

    // Tame
    layout.add (onParam (ids::tameOn, "Tame on"));
    layout.add (hzParam (ids::tameFreq, "Tame freq", 3000.0f, 12000.0f, 6500.0f, 6500.0f));
    layout.add (pctParam (ids::tameAmount, "Tame", 30.0f));

    // Press
    layout.add (onParam (ids::pressOn, "Press on"));
    layout.add (dbParam (ids::pressThreshold, "Threshold", -60.0f, 0.0f, -18.0f));
    layout.add (std::make_unique<APF> (pid (ids::pressRatio), "Ratio", skewed (1.0f, 20.0f, 4.0f, 0.01f), 3.0f,
                                       Attr().withStringFromValueFunction (formatRatio)));
    layout.add (std::make_unique<APF> (pid (ids::pressAttack), "Attack", skewed (0.1f, 100.0f, 10.0f, 0.01f), 10.0f,
                                       Attr().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<APF> (pid (ids::pressRelease), "Release", skewed (10.0f, 1000.0f, 120.0f, 0.1f), 120.0f,
                                       Attr().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (dbParam (ids::pressMakeup, "Makeup", 0.0f, 24.0f, 2.0f));
    layout.add (pctParam (ids::pressParallel, "Parallel", 0.0f));

    // Heat
    layout.add (onParam (ids::heatOn, "Heat on"));
    layout.add (dbParam (ids::heatDrive, "Drive", 0.0f, 24.0f, 4.0f));
    layout.add (pctParam (ids::heatMix, "Heat mix", 35.0f));

    // Space
    layout.add (onParam (ids::spaceOn, "Space on"));
    layout.add (std::make_unique<APC> (pid (ids::delayTime), "Delay time", delayDivisionNames(), 3));
    layout.add (pctParam (ids::delayFeedback, "Feedback", 25.0f));
    layout.add (pctParam (ids::delayMix, "Echo", 10.0f));
    layout.add (pctParam (ids::reverbSize, "Size", 45.0f));
    layout.add (pctParam (ids::reverbMix, "Verb", 12.0f));

    // Master
    layout.add (pctParam (ids::amount, "Amount", 100.0f));
    layout.add (dbParam (ids::outputGain, "Output", -24.0f, 12.0f, 0.0f));
    layout.add (onParam (ids::ceilingOn, "Ceiling on"));

    //==========================================================================
    // OJU 2.0 - every new module defaults to off, so v1 presets sound exactly like v1.
    layout.add (choice2 (ids::role, "Role", { "Vocal", "Beat" }, 0));
    layout.add (choice2 (ids::linkGroup, "Link group", { "A", "B", "C", "D" }, 0));
    layout.add (choice2 (ids::latencyMode, "Latency mode", { "Mix", "Track" }, 0));

    layout.add (on2 (ids::denoiseOn, "Denoise on"));
    layout.add (pct2 (ids::denoiseAmount, "Denoise", 60.0f));
    layout.add (pct2 (ids::roomAmount, "Room", 50.0f));

    layout.add (on2 (ids::plosiveOn, "Plosive tamer on"));
    layout.add (pct2 (ids::plosiveAmount, "Plosives", 50.0f));

    layout.add (on2 (ids::tuneOn, "Tune on"));
    layout.add (choice2 (ids::tuneKeySource, "Key source", { "Auto", "Manual" }, 0));
    layout.add (choice2 (ids::tuneKey, "Key", keyNames(), 0));
    layout.add (choice2 (ids::tuneScale, "Scale", scaleNames(), 1));
    layout.add (pct2 (ids::tuneSpeed, "Retune speed", 40.0f));
    layout.add (pct2 (ids::tuneHumanize, "Humanize", 50.0f));
    layout.add (pct2 (ids::tuneMix, "Tune amount", 100.0f));

    layout.add (on2 (ids::deessAuto, "De-esser auto band"));

    layout.add (on2 (ids::levelOn, "Leveler on"));
    layout.add (pct2 (ids::levelAmount, "Leveler", 50.0f));

    layout.add (on2 (ids::breathOn, "Breath control on"));
    layout.add (pct2 (ids::breathAmount, "Breaths", 50.0f));

    layout.add (on2 (ids::doubleOn, "Double on"));
    layout.add (pct2 (ids::doubleAmount, "Double", 35.0f));
    layout.add (pct2 (ids::width, "Width", 60.0f));
    layout.add (on2 (ids::hookOnly, "Hook only"));

    layout.add (on2 (ids::echoOn, "Delay on", true));
    layout.add (pct2 (ids::echoDuck, "Delay duck", 0.0f));
    layout.add (on2 (ids::verbOn, "Reverb on", true));
    layout.add (choice2 (ids::verbType, "Reverb type", verbTypeNames(), 0));
    layout.add (pct2 (ids::verbDuck, "Reverb duck", 0.0f));

    layout.add (on2 (ids::riderOn, "Rider on"));
    layout.add (pct2 (ids::riderAmount, "Rider", 50.0f));
    layout.add (on2 (ids::limiterOn, "Limiter on"));
    layout.add (std::make_unique<APF> (pid2 (ids::limiterCeiling), "Ceiling", juce::NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -1.0f,
                                       Attr().withLabel ("dB").withStringFromValueFunction (formatDb)));

    layout.add (on2 (ids::carveOn, "Carve on"));
    layout.add (pct2 (ids::carveDepth, "Carve depth", 50.0f));

    return layout;
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray list {
        ids::inputGain, ids::outputGain, ids::amount, ids::ceilingOn,
        ids::lowCutOn, ids::lowCutFreq, ids::eqOn, ids::bodyGain, ids::mudFreq, ids::mudGain,
        ids::presenceFreq, ids::presenceGain, ids::airGain,
        ids::tameOn, ids::tameFreq, ids::tameAmount,
        ids::pressOn, ids::pressThreshold, ids::pressRatio, ids::pressAttack, ids::pressRelease,
        ids::pressMakeup, ids::pressParallel,
        ids::heatOn, ids::heatDrive, ids::heatMix,
        ids::spaceOn, ids::delayTime, ids::delayFeedback, ids::delayMix, ids::reverbSize, ids::reverbMix,
        // OJU 2.0
        ids::denoiseOn, ids::denoiseAmount, ids::roomAmount, ids::plosiveOn, ids::plosiveAmount,
        ids::tuneOn, ids::tuneKeySource, ids::tuneKey, ids::tuneScale, ids::tuneSpeed, ids::tuneHumanize, ids::tuneMix,
        ids::deessAuto, ids::levelOn, ids::levelAmount, ids::breathOn, ids::breathAmount,
        ids::doubleOn, ids::doubleAmount, ids::width, ids::hookOnly,
        ids::echoOn, ids::echoDuck, ids::verbOn, ids::verbType, ids::verbDuck,
        ids::riderOn, ids::riderAmount, ids::limiterOn, ids::limiterCeiling
    };
    return list;
}

const juce::StringArray& v2ParameterIds()
{
    static const juce::StringArray list {
        ids::role, ids::linkGroup, ids::latencyMode,
        ids::denoiseOn, ids::denoiseAmount, ids::roomAmount, ids::plosiveOn, ids::plosiveAmount,
        ids::tuneOn, ids::tuneKeySource, ids::tuneKey, ids::tuneScale, ids::tuneSpeed, ids::tuneHumanize, ids::tuneMix,
        ids::deessAuto, ids::levelOn, ids::levelAmount, ids::breathOn, ids::breathAmount,
        ids::doubleOn, ids::doubleAmount, ids::width, ids::hookOnly,
        ids::echoOn, ids::echoDuck, ids::verbOn, ids::verbType, ids::verbDuck,
        ids::riderOn, ids::riderAmount, ids::limiterOn, ids::limiterCeiling, ids::carveOn, ids::carveDepth
    };
    return list;
}

} // namespace oju
