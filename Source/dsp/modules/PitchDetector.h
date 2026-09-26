#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include "../Svf.h"

namespace oju
{
/*  YIN pitch detector (de Cheveigné & Kawahara, 2002), our own implementation.

    The input is low-passed and decimated to ~12 kHz (voices sit well below 1.1 kHz),
    then every ~5 ms the cumulative-mean-normalised difference function is evaluated
    over a ~16 ms window. Adds a voicing probability (1 - aperiodicity, gated by level)
    and a simple octave-jump guard. Allocation-free after prepare().                   */
class PitchDetector
{
public:
    static constexpr double minF0 = 70.0, maxF0 = 1100.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 12000.0));
        fd = fs / decim;
        maxLag = (int) std::ceil (fd / minF0) + 2;
        minLag = std::max (2, (int) std::floor (fd / maxF0));
        window = (int) std::ceil (fd * 0.016);
        hopD = std::max (8, (int) std::lround (fd * 0.005));
        size = window + maxLag + 4;
        buf.assign ((size_t) size, 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        const double cut = std::min (3500.0, fd * 0.4);
        c1 = makeSvf (SvfType::lowpass, fs, cut, 0.5412);
        c2 = makeSvf (SvfType::lowpass, fs, cut, 1.3066);
        reset();
    }

    void reset()
    {
        s1.reset(); s2.reset();
        std::fill (buf.begin(), buf.end(), 0.0f);
        write = 0; filled = 0; phase = 0; hopCount = 0; inputCount = 0;
        f0 = 0.0f; prob = 0.0f; lastVoicedF0 = 0.0f;
    }

    // Push one input sample. Returns true when a new estimate is available.
    bool push (float x) noexcept
    {
        ++inputCount;
        const float y = s2.process (c2, s1.process (c1, x));
        if (++phase < decim)
            return false;
        phase = 0;
        buf[(size_t) write] = y;
        write = (write + 1) % size;
        filled = std::min (filled + 1, size);
        if (++hopCount < hopD || filled < size)
            return false;
        hopCount = 0;
        analyse();
        return true;
    }

    float getF0() const noexcept          { return f0; }      // Hz, 0 when unvoiced
    float getProbability() const noexcept { return prob; }    // 0..1
    // Input-sample time (count of pushed samples) at the centre of the analysed window.
    int64_t getCentreTime() const noexcept { return inputCount - (int64_t) latencySamples(); }
    int latencySamples() const noexcept    { return ((window + maxLag) / 2 + 4) * decim; }

private:
    float at (int i) const noexcept { return buf[(size_t) ((write + i) % size)]; }   // i = 0: oldest

    void analyse() noexcept
    {
        // level gate
        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += (double) at (j) * at (j);
        const double rms = std::sqrt (energy / window);
        if (rms < 0.0015)   // about -56 dBFS
        {
            unvoiced();
            return;
        }

        // difference function and its cumulative-mean normalisation
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const float delta = at (j) - at (j + tau);
                d += (double) delta * delta;
            }
            running += d;
            diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        // first dip under the threshold (the YIN rule), else the global minimum
        constexpr float threshold = 0.15f;
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = minLag;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (diff[(size_t) tau] < diff[(size_t) best]) best = tau;
        }

        const float aperiodicity = diff[(size_t) best];
        if (aperiodicity > 0.35f)
        {
            unvoiced();
            return;
        }

        // parabolic refinement
        float tauF = (float) best;
        if (best > 1 && best < maxLag)
        {
            const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f)
                tauF += 0.5f * (a - c) / den;
        }
        float est = (float) (fd / tauF);

        // octave guard: a sudden jump of an octave from a stable note is usually an error
        if (lastVoicedF0 > 0.0f)
        {
            const float r = est / lastVoicedF0;
            if (std::abs (r - 2.0f) < 0.08f || std::abs (r - 0.5f) < 0.04f)
            {
                const int alt = (int) std::lround (fd / lastVoicedF0);
                if (alt >= minLag && alt < maxLag && diff[(size_t) alt] < aperiodicity + 0.08f)
                    est = lastVoicedF0;
            }
        }

        f0 = est;
        prob = std::max (0.0f, std::min (1.0f, 1.0f - aperiodicity / 0.35f));
        lastVoicedF0 = est;
        unvoicedHops = 0;
    }

    void unvoiced() noexcept
    {
        f0 = 0.0f; prob = 0.0f;
        if (++unvoicedHops > 20)   // ~100 ms: forget the last note
            lastVoicedF0 = 0.0f;
    }

    double fs = 48000.0, fd = 12000.0;
    int decim = 4, maxLag = 172, minLag = 10, window = 192, hopD = 60, size = 400;
    std::vector<float> buf, diff;
    int write = 0, filled = 0, phase = 0, hopCount = 0, unvoicedHops = 0;
    int64_t inputCount = 0;
    SvfCoeffs c1, c2;
    SvfState s1, s2;
    float f0 = 0.0f, prob = 0.0f, lastVoicedF0 = 0.0f;
};

} // namespace oju
