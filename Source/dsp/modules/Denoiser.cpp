#include "Denoiser.h"
#include <mutex>

extern "C"
{
#include "rnnoise.h"
}

namespace oju
{
namespace
{
    // RNNoise band edges in units of 200 Hz (eband5ms in denoise.c)
    constexpr int bandEdges[22] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 78, 100 };

    // RNNoise sets up its FFT tables on first use; do that once, off the audio thread.
    void warmUpRnnoise()
    {
        static std::once_flag once;
        std::call_once (once, []
        {
            if (auto* st = rnnoise_create (nullptr))
            {
                float frame[480] {}, gains[32] {};
                rnnoise_get_band_gains (st, gains, frame);
                rnnoise_destroy (st);
            }
        });
    }
}

Denoiser::Denoiser()
{
    warmUpRnnoise();
    rnn = rnnoise_create (nullptr);
}

Denoiser::~Denoiser()
{
    if (rnn != nullptr)
        rnnoise_destroy (rnn);
}

float Denoiser::profileFreq (int band) const noexcept
{
    return 40.0f * std::pow (500.0f, (float) band / (float) (profileBands - 1));   // 40 Hz .. 20 kHz
}

void Denoiser::prepare (double sampleRate, int maxBlock)
{
    fs = sampleRate;
    fftOrder = juce::jlimit (9, 13, (int) std::ceil (std::log2 (0.02 * fs)));
    fftSize = 1 << fftOrder;
    hop = fftSize / 4;
    bins = fftSize / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)   // sqrt periodic Hann: analysis x synthesis = Hann (sums to 2 at 75 % overlap)
        window[(size_t) i] = std::sqrt (0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize)));

    for (int c = 0; c < maxChannels; ++c)
    {
        inRing[(size_t) c].assign ((size_t) fftSize, 0.0f);
        outAccum[(size_t) c].assign ((size_t) fftSize, 0.0f);
    }
    frame.assign ((size_t) (fftSize * 2 * maxChannels), 0.0f);
    monoScratch.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    binGain.assign ((size_t) bins, 1.0f);
    binPower.assign ((size_t) bins, 0.0f);
    noiseProfile.assign ((size_t) bins, 0.0f);
    learnAccum.assign ((size_t) bins, 0.0);
    binHz.resize ((size_t) bins);
    for (int k = 0; k < bins; ++k)
        binHz[(size_t) k] = (float) (k * fs / fftSize);

    resampleStep = fs / 48000.0;

    // fresh network state (allocates, so only here - never on the audio thread)
    if (rnn != nullptr)
        rnnoise_destroy (rnn);
    rnn = rnnoise_create (nullptr);
    learnTarget = (int) std::ceil (2.0 * fs / hop);

    // if a profile was loaded before prepare, rebuild it for this sample rate
    if (roomValid.load())
    {
        std::array<float, profileBands> copy = learnedOut;
        setProfile (copy, true);
    }
    reset();
}

void Denoiser::reset()
{
    for (int c = 0; c < maxChannels; ++c)
    {
        std::fill (inRing[(size_t) c].begin(), inRing[(size_t) c].end(), 0.0f);
        std::fill (outAccum[(size_t) c].begin(), outAccum[(size_t) c].end(), 0.0f);
    }
    std::fill (binGain.begin(), binGain.end(), 1.0f);
    std::fill (binPower.begin(), binPower.end(), 0.0f);
    bandGains.fill (1.0f);
    inPos = hopCount = outPos = 0;
    rnnFill = 0;
    resamplePos = 0.0;
    lastInput = 0.0f;
    vadValue = 0.0f;
}

//==============================================================================
void Denoiser::setProfile (const std::array<float, profileBands>& dbValues, bool valid)
{
    pendingProfile = dbValues;
    pendingState.store (valid ? 1 : 2, std::memory_order_release);
    if (valid)
        learnedOut = dbValues;
}

void Denoiser::applyPendingProfile() noexcept
{
    const int st = pendingState.exchange (0, std::memory_order_acquire);
    if (st == 0 || noiseProfile.empty())
        return;
    if (st == 2)
    {
        roomValid.store (false);
        return;
    }
    // log-frequency interpolation of the 64 stored bands onto this FFT's bins
    for (int k = 0; k < bins; ++k)
    {
        const float f = juce::jmax (40.0f, binHz[(size_t) k]);
        const float pos = std::log (f / 40.0f) / std::log (500.0f) * (float) (profileBands - 1);
        const int b0 = juce::jlimit (0, profileBands - 2, (int) pos);
        const float t = juce::jlimit (0.0f, 1.0f, pos - (float) b0);
        const float db = pendingProfile[(size_t) b0] + t * (pendingProfile[(size_t) b0 + 1] - pendingProfile[(size_t) b0]);
        noiseProfile[(size_t) k] = std::pow (10.0f, db / 10.0f);
    }
    roomValid.store (true);
}

bool Denoiser::takeLearnedProfile (std::array<float, profileBands>& out)
{
    if (! learnedReady.exchange (false))
        return false;
    out = learnedOut;
    return true;
}

//==============================================================================
void Denoiser::runRnnoise (const float* mono, int n) noexcept
{
    if (rnn == nullptr)
        return;
    for (int i = 0; i < n; ++i)
    {
        const float x = mono[i];
        while (resamplePos < 1.0)
        {
            const float y = lastInput + (float) resamplePos * (x - lastInput);
            rnnFrame[(size_t) rnnFill++] = y * 32768.0f;   // RNNoise works in 16-bit scale
            if (rnnFill == 480)
            {
                float g[32] {};
                vadValue = rnnoise_get_band_gains (rnn, g, rnnFrame.data());
                for (int b = 0; b < 22; ++b)
                    bandGains[(size_t) b] = g[b];
                rnnFill = 0;
            }
            resamplePos += resampleStep;
        }
        resamplePos -= 1.0;
        lastInput = x;
    }
    vad.store (vadValue, std::memory_order_relaxed);
}

void Denoiser::process (float* const* ch, int nch, int n, float amount, float room, bool extreme) noexcept
{
    applyPendingProfile();
    if (learnRequest.exchange (false))
    {
        std::fill (learnAccum.begin(), learnAccum.end(), 0.0);
        learnFrames = 0;
        learnProgressValue.store (0.0f);
        learning.store (true);
    }

    nch = juce::jmin (nch, maxChannels);
    n = juce::jmin (n, (int) monoScratch.size());
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < nch; ++c)
            m += ch[c][i];
        monoScratch[(size_t) i] = m / (float) nch;
    }
    runRnnoise (monoScratch.data(), n);

    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < nch; ++c)
        {
            inRing[(size_t) c][(size_t) inPos] = ch[c][i];
            auto& acc = outAccum[(size_t) c];
            ch[c][i] = acc[(size_t) outPos];
            acc[(size_t) outPos] = 0.0f;
        }
        inPos = (inPos + 1) % fftSize;
        outPos = (outPos + 1) % fftSize;
        if (++hopCount == hop)
        {
            hopCount = 0;
            processFrame (nch, amount, room, extreme);
        }
    }
}

void Denoiser::processFrame (int nch, float amount, float room, bool extreme) noexcept
{
    const int N = fftSize;

    // ---- analysis
    for (int c = 0; c < nch; ++c)
    {
        float* f = frame.data() + (size_t) c * (size_t) (2 * N);
        const auto& ring = inRing[(size_t) c];
        for (int j = 0; j < N; ++j)
            f[j] = ring[(size_t) ((inPos + j) % N)] * window[(size_t) j];
        std::fill (f + N, f + 2 * N, 0.0f);
        fft->performRealOnlyForwardTransform (f, true);
    }

    // ---- gains
    const float floorDb = extreme ? -30.0f : -18.0f;
    const bool useRoom = room > 0.0f && roomValid.load (std::memory_order_relaxed);
    const float floorLin = std::pow (10.0f, (floorDb * juce::jmax (amount, useRoom ? room : 0.0f)) / 20.0f);
    const float exponent = extreme ? 1.3f : 0.8f;
    const float release = std::exp (-(float) hop / (float) (fs * (extreme ? 0.08 : 0.15)));
    const float norm = 2.0f / (float) N;     // power normalisation (window energy), rate-independent profile
    const bool learnThisFrame = learning.load (std::memory_order_relaxed) && vadValue < 0.3f;   // silence only
    double gainSum = 0.0;

    for (int k = 0; k < bins; ++k)
    {
        float p = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            const float* f = frame.data() + (size_t) c * (size_t) (2 * N);
            p += f[2 * k] * f[2 * k] + f[2 * k + 1] * f[2 * k + 1];
        }
        p = p / (float) nch * norm;
        binPower[(size_t) k] = 0.6f * binPower[(size_t) k] + 0.4f * p;

        // RNNoise band gain, interpolated between band edges (as RNNoise's interp_band_gain)
        const float unit = binHz[(size_t) k] / 200.0f;
        float gr = bandGains[21];
        for (int b = 0; b < 21; ++b)
            if (unit < (float) bandEdges[b + 1])
            {
                const float t = (unit - (float) bandEdges[b]) / (float) (bandEdges[b + 1] - bandEdges[b]);
                gr = bandGains[(size_t) b] + t * (bandGains[(size_t) b + 1] - bandGains[(size_t) b]);
                break;
            }
        gr = std::pow (juce::jlimit (0.0f, 1.0f, gr), exponent);
        float g = 1.0f - amount * (1.0f - gr);

        // Learn Room fingerprint: steady background noise, spectral subtraction
        if (useRoom)
        {
            const float ratio = noiseProfile[(size_t) k] / (binPower[(size_t) k] + 1.0e-12f);
            const float gRoom = juce::jlimit (0.0f, 1.0f, 1.0f - room * 2.0f * ratio);
            g = juce::jmin (g, gRoom);
        }

        g = juce::jmax (g, floorLin);
        // rise at once, fall slowly: sustained notes and tails are never chopped
        binGain[(size_t) k] = juce::jmax (g, binGain[(size_t) k] * release);
        gainSum += binGain[(size_t) k];

        if (learnThisFrame)
            learnAccum[(size_t) k] += p;
    }
    reductionDb.store (-20.0f * std::log10 ((float) (gainSum / bins) + 1.0e-6f), std::memory_order_relaxed);

    // ---- Learn Room: only quiet, non-singing frames count towards the 2 seconds
    if (learnThisFrame && ++learnFrames >= learnTarget)
    {
        for (int b = 0; b < profileBands; ++b)
        {
            const float fc = profileFreq (b);
            const float lo = fc / std::pow (2.0f, 1.0f / 12.0f), hi = fc * std::pow (2.0f, 1.0f / 12.0f);
            double sum = 0.0; int cnt = 0;
            for (int k = 1; k < bins; ++k)
                if (binHz[(size_t) k] >= lo && binHz[(size_t) k] <= hi) { sum += learnAccum[(size_t) k]; ++cnt; }
            if (cnt == 0)
            {
                const int k = juce::jlimit (1, bins - 1, (int) std::round ((double) fc * fftSize / fs));
                sum = learnAccum[(size_t) k]; cnt = 1;
            }
            learnedOut[(size_t) b] = (float) (10.0 * std::log10 (sum / cnt / learnFrames + 1.0e-20));
        }
        pendingProfile = learnedOut;
        pendingState.store (1, std::memory_order_release);
        learnedReady.store (true);
        learning.store (false);
    }
    if (learning.load (std::memory_order_relaxed))
        learnProgressValue.store ((float) learnFrames / (float) learnTarget);

    // ---- apply and resynthesise
    for (int c = 0; c < nch; ++c)
    {
        float* f = frame.data() + (size_t) c * (size_t) (2 * N);
        for (int k = 0; k < bins; ++k)
        {
            f[2 * k] *= binGain[(size_t) k];
            f[2 * k + 1] *= binGain[(size_t) k];
        }
        fft->performRealOnlyInverseTransform (f);
        auto& acc = outAccum[(size_t) c];
        for (int j = 0; j < N; ++j)
            acc[(size_t) ((outPos + j) % N)] += f[j] * window[(size_t) j] * 0.5f;
    }
}

} // namespace oju
