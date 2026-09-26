#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Svf.h"

namespace oju
{
// Cleanup: tames P/B pops. Plosives are bursts of energy below ~150 Hz that arrive
// without matching midrange energy. Only the low band is turned down, briefly.
class PlosiveTamer
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        fs = sampleRate;
        split.prepare ({ fs, (juce::uint32) juce::jmax (1, maxBlock), (juce::uint32) juce::jmax (1, numChannels) });
        split.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        split.setCutoffFrequency (150.0f);
        cMid = makeSvf (SvfType::bandpass, fs, 900.0, 0.6);
        attLo = coef (0.0003); relLo = coef (0.050);
        attMid = coef (0.001); relMid = coef (0.050);
        gAtt = coef (0.0005); gRel = coef (0.080);
        reset();
    }

    void reset()
    {
        split.reset();
        sMid.reset();
        envLo = envMid = redDb = 0.0f;
    }

    // amount 0..1. Returns the peak reduction (dB) in this block.
    float process (float* const* ch, int nch, int n, float amount) noexcept
    {
        float maxRed = 0.0f;
        const float thresh = 9.0f - 6.0f * amount;              // low band this much above the mids = a pop
        const float maxDb = 6.0f + 16.0f * amount;
        for (int i = 0; i < n; ++i)
        {
            float lo[2] {}, hi[2] {}, loLevel = 0.0f, mono = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                split.processSample (c, ch[c][i], lo[c], hi[c]);
                loLevel = juce::jmax (loLevel, std::abs (lo[c]));
                mono += ch[c][i];
            }
            const float midLevel = std::abs (sMid.process (cMid, mono / (float) nch));
            envLo  = loLevel  + (loLevel  > envLo  ? attLo  : relLo)  * (envLo - loLevel);
            envMid = midLevel + (midLevel > envMid ? attMid : relMid) * (envMid - midLevel);

            float target = 0.0f;
            if (envLo > 0.015f)
            {
                const float ratioDb = 20.0f * std::log10 ((envLo + 1.0e-9f) / (envMid + 1.0e-6f));
                target = juce::jlimit (0.0f, maxDb, (ratioDb - thresh) * 1.5f);
            }
            redDb = target + (target > redDb ? gAtt : gRel) * (redDb - target);
            maxRed = juce::jmax (maxRed, redDb);
            const float g = std::exp (-redDb * 0.115129255f);
            for (int c = 0; c < nch; ++c)
                ch[c][i] = lo[c] * g + hi[c];
        }
        return maxRed;
    }

private:
    float coef (double seconds) const { return (float) std::exp (-1.0 / (seconds * fs)); }

    double fs = 48000.0;
    juce::dsp::LinkwitzRileyFilter<float> split;
    SvfCoeffs cMid;
    SvfState sMid;
    float envLo = 0.0f, envMid = 0.0f, redDb = 0.0f;
    float attLo = 0.0f, relLo = 0.0f, attMid = 0.0f, relMid = 0.0f, gAtt = 0.0f, gRel = 0.0f;
};

} // namespace oju
