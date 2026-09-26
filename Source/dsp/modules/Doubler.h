#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../Svf.h"
#include <vector>

namespace oju
{
// Double + Width: two synthetic double-tracked voices (slightly late, gently drifting
// in time and pitch, like a second take), panned left and right, plus mid/side width.
// "Hook only" brings the double in on the louder sections of the song.
class Doubler
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        size = (int) std::ceil (0.05 * fs) + 4;
        line.assign ((size_t) size, 0.0f);
        write = 0;
        cHp = makeSvf (SvfType::highpass, fs, 160.0, 0.707);
        cLp = makeSvf (SvfType::lowpass, fs, juce::jmin (9000.0, fs * 0.4), 0.707);
        shortCoef = coef (1.5);
        hookCoef = coef (0.35);
        mix.reset (fs, 0.05);
        reset();
    }

    void reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        for (auto& v : voices) v = Voice();
        voices[0].base = 0.014f; voices[0].rate = 0.37f; voices[0].seed = 1;
        voices[1].base = 0.022f; voices[1].rate = 0.53f; voices[1].seed = 7;
        shortDb = longDb = -60.0f; hook = 0.0f; singing = 0.0f;
    }

    // amount 0..1 (double level), width 0..1 (0.5 = natural), hookOnly as described.
    // Returns how much the double is engaged (0..1), for the UI.
    float process (float* const* ch, int nch, int n, float amount, float width, bool hookOnly) noexcept
    {
        mix.setTargetValue (amount);
        const float sideGain = width * 2.0f;
        float engaged = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < nch; ++c)
                mono += ch[c][i];
            mono /= (float) nch;
            line[(size_t) write] = mono;

            // loudness: is this the hook?
            const float p = mono * mono;
            const float pDb = 10.0f * std::log10 (p + 1.0e-12f);
            shortDb = pDb + shortCoef * (shortDb - pDb);
            if (pDb > -60.0f)
            {
                // the song's average: learns fast at first, then settles to a ~20 s memory
                singing = juce::jmin (singing + 1.0f, (float) (20.0 * fs));
                const float c = std::exp (-1.0f / juce::jmax ((float) (0.5 * fs), singing));
                longDb = pDb + c * (longDb - pDb);
            }
            const float hookTarget = hookOnly ? (shortDb > longDb + 1.5f ? 1.0f : 0.0f) : 1.0f;
            hook = hookTarget + hookCoef * (hook - hookTarget);

            const float level = mix.getNextValue() * hook * 0.8f;
            engaged = juce::jmax (engaged, hook * (amount > 0.0f ? 1.0f : 0.0f));

            float out[2] {};
            for (int v = 0; v < 2; ++v)
            {
                auto& voice = voices[v];
                // slow random drift: +/- 1.5 ms of timing, a few cents of pitch
                voice.phase += voice.rate / (float) fs;
                if (voice.phase >= 1.0f)
                {
                    voice.phase -= 1.0f;
                    voice.from = voice.to;
                    voice.seed = voice.seed * 1103515245u + 12345u;
                    voice.to = ((float) ((voice.seed >> 8) & 0xffff) / 65535.0f) * 2.0f - 1.0f;
                }
                const float t = voice.phase * voice.phase * (3.0f - 2.0f * voice.phase);
                const float drift = voice.from + (voice.to - voice.from) * t;
                const float delay = (voice.base + 0.0015f * drift) * (float) fs;

                float rp = (float) write - delay;
                while (rp < 0.0f) rp += (float) size;
                const int r0 = (int) rp;
                const int r1 = (r0 + 1) % size;
                const float fr = rp - (float) r0;
                float y = line[(size_t) r0] + fr * (line[(size_t) r1] - line[(size_t) r0]);
                y = voice.lp.process (cLp, voice.hp.process (cHp, y));
                out[v] = y * (v == 0 ? 1.0f : 0.92f);
            }
            write = (write + 1) % size;

            if (nch > 1)
            {
                float l = ch[0][i] + level * out[0];
                float r = ch[1][i] + level * out[1];
                const float m = 0.5f * (l + r), s = 0.5f * (l - r) * sideGain;
                ch[0][i] = m + s;
                ch[1][i] = m - s;
            }
            else
            {
                ch[0][i] += level * 0.5f * (out[0] + out[1]);
            }
        }
        return engaged;
    }

private:
    float coef (double s) const { return (float) std::exp (-1.0 / (s * fs)); }

    struct Voice
    {
        float base = 0.014f, rate = 0.4f, phase = 0.0f, from = 0.0f, to = 0.0f;
        unsigned int seed = 1;
        SvfState hp, lp;
    };

    double fs = 48000.0;
    std::vector<float> line;
    int size = 1, write = 0;
    Voice voices[2];
    SvfCoeffs cHp, cLp;
    float shortDb = -60.0f, longDb = -60.0f, hook = 0.0f, singing = 0.0f;
    float shortCoef = 0.0f, hookCoef = 0.0f;
    juce::SmoothedValue<float> mix;
};

} // namespace oju
