#pragma once

#include <cmath>
#include <complex>

namespace oju
{
/*  Topology-preserving state-variable filter (Simper / Cytomic form).
    Coefficients can be changed every few samples without clicks or blow-ups,
    which makes it ideal for an EQ that the brain moves while audio plays. */
enum class SvfType { highpass, bell, lowShelf, highShelf, bandpass, lowpass };

struct SvfCoeffs
{
    float g = 0.0f, k = 1.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
};

inline SvfCoeffs makeSvf (SvfType type, double sampleRate, double freq, double q, double gainDb = 0.0)
{
    constexpr double pi = 3.14159265358979323846;
    freq = std::fmin (std::fmax (freq, 10.0), sampleRate * 0.49);

    const double A = std::pow (10.0, gainDb / 40.0);
    double g = std::tan (pi * freq / sampleRate);
    double k = 1.0 / q;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;

    switch (type)
    {
        case SvfType::highpass:  m0 = 1.0; m1 = -k; m2 = -1.0; break;
        case SvfType::bell:      k = 1.0 / (q * A); m1 = k * (A * A - 1.0); break;
        case SvfType::lowShelf:  g /= std::sqrt (A); m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
        case SvfType::highShelf: g *= std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
        case SvfType::bandpass:  m0 = 0.0; m1 = k; m2 = 0.0; break;   // unity gain at the centre
        case SvfType::lowpass:   m0 = 0.0; m1 = 0.0; m2 = 1.0; break;
    }

    SvfCoeffs c;
    c.g  = (float) g;
    c.k  = (float) k;
    c.a1 = (float) (1.0 / (1.0 + g * (g + k)));
    c.a2 = (float) (g * c.a1);
    c.a3 = (float) (g * c.a2);
    c.m0 = (float) m0;
    c.m1 = (float) m1;
    c.m2 = (float) m2;
    return c;
}

// Exact magnitude of the digital filter at 'freq' (used by the EQ curve display).
inline double svfMagnitude (const SvfCoeffs& c, double freq, double sampleRate)
{
    constexpr double pi = 3.14159265358979323846;
    const double w = std::tan (pi * std::fmin (freq, sampleRate * 0.4999) / sampleRate) / (double) c.g;
    const std::complex<double> s (0.0, w);
    const auto den = s * s + (double) c.k * s + 1.0;
    const auto num = (double) c.m0 * den + (double) c.m1 * s + (double) c.m2;
    return std::abs (num / den);
}

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    inline float process (const SvfCoeffs& c, float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // Kill denormals / runaway after silence
    void sanitise() noexcept
    {
        if (! (std::abs (ic1) < 1.0e6f)) ic1 = 0.0f;
        if (! (std::abs (ic2) < 1.0e6f)) ic2 = 0.0f;
    }
};

} // namespace oju
