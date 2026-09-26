#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "../Svf.h"

namespace oju
{
// Carve: a dynamic EQ on the beat, driven by the vocal (through Beat Link). While the
// vocal sings, the beat dips where the voice lives; in the gaps the beat comes back.
class Carve
{
public:
    static constexpr int bands = 3;
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        att = (float) std::exp (-1.0 / (0.020 * fs));
        rel = (float) std::exp (-1.0 / (0.250 * fs));
        reset();
    }

    void reset()
    {
        for (auto& b : state) for (auto& s : b) s.reset();
        env = 0.0f;
        countdown = 0;
        active = false;
        for (int i = 0; i < bands; ++i) coeffs[(size_t) i] = makeSvf (SvfType::bell, fs, 1000.0, 1.0, 0.0);
    }

    // activity: 0..1 from the vocal; maxCutDb: how deep at full activity. Returns the current dip (dB).
    float process (float* const* ch, int nch, int n, float activity, float maxCutDb,
                   const std::array<float, bands>& hz, const std::array<float, bands>& weight) noexcept
    {
        constexpr int interval = 32;
        const float qs[bands] = { 1.2f, 0.9f, 1.0f };
        for (int i = 0; i < n; ++i)
        {
            env = activity + (activity > env ? att : rel) * (env - activity);
            if (--countdown <= 0)
            {
                countdown = interval;
                for (int b = 0; b < bands; ++b)
                    coeffs[(size_t) b] = makeSvf (SvfType::bell, fs, juce::jlimit (60.0f, 12000.0f, hz[(size_t) b]), qs[b],
                                                  -maxCutDb * env * weight[(size_t) b]);
            }
            if (maxCutDb * env < 0.005f)
            {
                if (active)   // fully released: step out of the way (the beat is untouched)
                {
                    for (auto& b : state) for (auto& st : b) st.reset();
                    active = false;
                }
                continue;
            }
            active = true;
            for (int c = 0; c < juce::jmin (nch, maxChannels); ++c)
            {
                float y = ch[c][i];
                for (int b = 0; b < bands; ++b)
                    y = state[(size_t) b][(size_t) c].process (coeffs[(size_t) b], y);
                ch[c][i] = y;
            }
        }
        return maxCutDb * env;
    }

private:
    double fs = 48000.0;
    float att = 0.0f, rel = 0.0f, env = 0.0f;
    bool active = false;
    int countdown = 0;
    std::array<SvfCoeffs, bands> coeffs;
    std::array<std::array<SvfState, maxChannels>, bands> state;
};

} // namespace oju
