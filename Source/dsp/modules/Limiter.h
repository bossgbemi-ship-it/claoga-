#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace oju
{
// Output safety limiter. Mix mode: 1.5 ms lookahead (clean, reported as latency).
// Track mode: no lookahead (zero latency). A final clamp guarantees the ceiling.
class Limiter
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        lookahead = juce::jmax (1, (int) std::ceil (0.0015 * fs));
        ringSize = lookahead + juce::jmax (1, maxBlock) + 2;
        for (auto& r : ring) r.assign ((size_t) ringSize, 0.0f);
        winVal.assign ((size_t) lookahead + 2, 1.0f);
        winPos.assign ((size_t) lookahead + 2, 0);
        relCoef = (float) std::exp (-1.0 / (0.080 * fs));
        bypass.reset (fs, 0.03);
        reset();
    }

    void reset()
    {
        for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f);
        write = 0; head = tail = 0; counter = 0;
        gain = 1.0f;
    }

    int latencySamples (bool trackMode) const noexcept { return trackMode ? 0 : lookahead; }

    // Returns the maximum gain reduction (dB) in this block.
    float process (float* const* ch, int nch, int n, float ceilingDb, bool trackMode, bool bypassed) noexcept
    {
        const float ceil = juce::Decibels::decibelsToGain (ceilingDb);
        const int la = trackMode ? 0 : lookahead;
        const float attAlpha = la > 0 ? 1.0f - std::exp (-4.0f / (float) la) : 1.0f;
        bypass.setTargetValue (bypassed ? 1.0f : 0.0f);
        float minGain = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                ring[(size_t) c][(size_t) write] = ch[c][i];
                peak = juce::jmax (peak, std::abs (ch[c][i]));
            }
            const float required = peak > ceil ? ceil / peak : 1.0f;

            // sliding-window minimum over the lookahead (monotonic queue, no allocation)
            const int cap = (int) winVal.size();
            while (head != tail && winVal[(size_t) ((tail - 1 + cap) % cap)] >= required)
                tail = (tail - 1 + cap) % cap;
            winVal[(size_t) tail] = required;
            winPos[(size_t) tail] = counter;
            tail = (tail + 1) % cap;
            while (winPos[(size_t) head] <= counter - la - 1)
                head = (head + 1) % cap;
            const float target = winVal[(size_t) head];
            ++counter;

            gain = target < gain ? gain + (target - gain) * attAlpha : target + relCoef * (gain - target);
            const float b = bypass.getNextValue();
            const float g = 1.0f + (gain - 1.0f) * (1.0f - b);
            minGain = juce::jmin (minGain, gain);

            int r = write - la;
            if (r < 0) r += ringSize;
            for (int c = 0; c < nch; ++c)
            {
                float y = ring[(size_t) c][(size_t) r] * g;
                const float clamped = juce::jlimit (-ceil, ceil, y);
                y += (1.0f - b) * (clamped - y);
                ch[c][i] = y;
            }
            write = (write + 1) % ringSize;
        }
        return -juce::Decibels::gainToDecibels (minGain, -60.0f);
    }

private:
    double fs = 48000.0;
    int lookahead = 72, ringSize = 1, write = 0;
    std::vector<float> ring[maxChannels];
    std::vector<float> winVal;
    std::vector<long long> winPos;
    int head = 0, tail = 0;
    long long counter = 0;
    float gain = 1.0f, relCoef = 0.999f;
    juce::SmoothedValue<float> bypass;
};

} // namespace oju
