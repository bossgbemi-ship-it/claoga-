#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../Svf.h"

namespace oju
{
// Breath control: finds breaths and turns them down (never deletes them).
// A breath is quiet, unvoiced, broadband air: much weaker than the singing, with
// almost no low-frequency (voiced) energy and without the high-frequency spike of an S.
class BreathControl
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        cLow = makeSvf (SvfType::lowpass, fs, 700.0, 0.707);
        cHigh = makeSvf (SvfType::highpass, fs, juce::jmin (5500.0, fs * 0.4), 0.707);
        envCoef = coef (0.010);
        voiceCoef = coef (2.0);
        attCoef = coef (0.015);
        relCoef = coef (0.060);
        holdSamples = (int) (0.030 * fs);
        reset();
    }

    void reset()
    {
        sLow.reset(); sHigh.reset();
        eAll = eLow = eHigh = 0.0f;
        voiceDb = -30.0f; redDb = 0.0f; held = 0;
        breathCount = 0; inBreath = false;
    }

    // amount 0..1 -> up to 18 dB down. Returns the deepest reduction in the block.
    float process (float* const* ch, int nch, int n, float amount) noexcept
    {
        const float depth = 18.0f * amount;
        float maxRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < nch; ++c)
                mono += ch[c][i];
            mono /= (float) nch;

            const float lo = sLow.process (cLow, mono);
            const float hi = sHigh.process (cHigh, mono);
            eAll  = mono * mono + envCoef * (eAll - mono * mono);
            eLow  = lo * lo + envCoef * (eLow - lo * lo);
            eHigh = hi * hi + envCoef * (eHigh - hi * hi);

            const float levelDb = 10.0f * std::log10 (eAll + 1.0e-12f);
            const float lowRatio = eLow / (eAll + 1.0e-12f);
            const float highRatio = eHigh / (eAll + 1.0e-12f);

            const bool voiced = lowRatio > 0.35f && levelDb > voiceDb - 20.0f;
            if (voiced)
                voiceDb = levelDb + voiceCoef * (voiceDb - levelDb);   // how loud the singing is

            const bool breathLike = levelDb < voiceDb - 8.0f && levelDb > voiceDb - 45.0f
                                    && lowRatio < 0.25f && highRatio < 0.45f && levelDb > -70.0f;
            held = breathLike ? juce::jmin (held + 1, holdSamples) : 0;
            const bool isBreath = held >= holdSamples;
            if (isBreath && ! inBreath) ++breathCount;
            inBreath = isBreath;

            const float target = isBreath ? depth : 0.0f;
            redDb = target + (target > redDb ? attCoef : relCoef) * (redDb - target);
            maxRed = juce::jmax (maxRed, redDb);
            const float g = std::exp (-redDb * 0.115129255f);
            for (int c = 0; c < nch; ++c)
                ch[c][i] *= g;
        }
        return maxRed;
    }

    int getBreathCount() const noexcept { return breathCount; }

private:
    float coef (double s) const { return (float) std::exp (-1.0 / (s * fs)); }

    double fs = 48000.0;
    SvfCoeffs cLow, cHigh;
    SvfState sLow, sHigh;
    float eAll = 0.0f, eLow = 0.0f, eHigh = 0.0f, voiceDb = -30.0f, redDb = 0.0f;
    float envCoef = 0.0f, voiceCoef = 0.0f, attCoef = 0.0f, relCoef = 0.0f;
    int held = 0, holdSamples = 1440, breathCount = 0;
    bool inBreath = false;
};

} // namespace oju
