#include "Tuner.h"

namespace oju
{
namespace
{
    // interval sets, as bit masks of the 12 pitch classes above the key
    constexpr int scaleMasks[] = {
        0b101010110101,   // major:            0 2 4 5 7 9 11
        0b010110101101,   // natural minor:    0 2 3 5 7 8 10
        0b111111111111,   // chromatic
        0b001010010101,   // major pentatonic: 0 2 4 7 9
        0b010010101001,   // minor pentatonic: 0 3 5 7 10
        0b100110101101,   // harmonic minor:   0 2 3 5 7 8 11
    };

    inline float hannRise (float u) noexcept { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u); }
}

bool Tuner::noteInScale (int pitchClass, int key, int scale) noexcept
{
    const int rel = ((pitchClass - key) % 12 + 12) % 12;
    const int maskBits = scaleMasks[juce::jlimit (0, 5, scale)];
    return (maskBits >> rel) & 1;
}

void Tuner::prepare (double sampleRate, int maxBlock)
{
    fs = sampleRate;
    pMax = (int) std::ceil (fs / PitchDetector::minF0);
    pMin = (int) std::floor (fs / PitchDetector::maxF0);
    pUnvoiced = (int) std::round (0.005 * fs);
    detector.prepare (fs);
    detectLag = detector.latencySamples() + (int) std::ceil (0.006 * fs);   // pitch known this far behind
    latency = detectLag + (int) std::ceil (3.0 * pMax);

    int needed = latency + 4 * pMax + juce::jmax (1, maxBlock) + 16;
    ringSize = 1;
    while (ringSize < needed) ringSize <<= 1;
    mask = ringSize - 1;

    for (int c = 0; c < maxChannels; ++c)
    {
        xRing[(size_t) c].assign ((size_t) ringSize, 0.0f);
        yRing[(size_t) c].assign ((size_t) ringSize, 0.0f);
    }
    wRing.assign ((size_t) ringSize, 0.0f);
    peakRing.assign ((size_t) ringSize, 0.0f);

    cPeak = makeSvf (SvfType::lowpass, fs, 1000.0, 0.707);
    reset();
}

void Tuner::reset()
{
    for (int c = 0; c < maxChannels; ++c)
    {
        std::fill (xRing[(size_t) c].begin(), xRing[(size_t) c].end(), 0.0f);
        std::fill (yRing[(size_t) c].begin(), yRing[(size_t) c].end(), 0.0f);
    }
    std::fill (wRing.begin(), wRing.end(), 0.0f);
    std::fill (peakRing.begin(), peakRing.end(), 0.0f);
    detector.reset();
    sPeak.reset();
    t = 0;
    frameCount = 0;
    markCount = 0;
    nextPredicted = 0;
    sNext = 0.0;
    kCursor = 0;
    synthStarted = false;
    centreMidi = 0.0f; appliedShift = 0.0f; currentTarget = -100.0f;
    wasVoiced = false;
}

//==============================================================================
void Tuner::onDetection (const Params& p) noexcept
{
    const float f0 = detector.getF0();
    const bool voiced = f0 > 0.0f && detector.getProbability() > 0.2f;
    const float hopTime = 0.005f;
    float desired = 0.0f;

    if (voiced)
    {
        const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);

        // the note's centre (for humanize): jumps on a new note, glides otherwise
        if (! wasVoiced || std::abs (midi - centreMidi) > 1.0f)
            centreMidi = midi;
        else
            centreMidi += (1.0f - std::exp (-hopTime / 0.12f)) * (midi - centreMidi);

        const float reference = midi + p.humanize * (centreMidi - midi);

        // nearest note in the scale, with hysteresis
        const float hysteresis = 0.65f - 0.1f * p.speed;   // Hard follows fast runs more eagerly
        if (currentTarget < 0.0f || std::abs (reference - currentTarget) > hysteresis
            || ! noteInScale ((int) std::lround (currentTarget) % 12, p.key, p.scale))
        {
            float best = std::round (reference), bestDist = 100.0f;
            for (int d = -6; d <= 6; ++d)
            {
                const int note = (int) std::lround (reference) + d;
                if (noteInScale (((note % 12) + 12) % 12, p.key, p.scale))
                {
                    const float dist = std::abs ((float) note - reference);
                    if (dist < bestDist) { bestDist = dist; best = (float) note; }
                }
            }
            currentTarget = best;
        }
        desired = (currentTarget - reference) * p.amount;

        const float tau = 0.35f * (1.0f - p.speed) * (1.0f - p.speed);   // Natural 350 ms .. Hard 0
        const float alpha = tau < 0.002f ? 1.0f : 1.0f - std::exp (-hopTime / tau);
        if (! wasVoiced)
            appliedShift = desired * (tau < 0.002f ? 1.0f : 0.5f);   // land on the note without a scoop
        else
            appliedShift += alpha * (desired - appliedShift);

        detectedMidi.store (midi, std::memory_order_relaxed);
        targetMidi.store (currentTarget, std::memory_order_relaxed);
    }
    else
    {
        appliedShift += (1.0f - std::exp (-hopTime / 0.03f)) * (0.0f - appliedShift);
        if (std::abs (appliedShift) < 0.001f) appliedShift = 0.0f;
        currentTarget = -100.0f;
        detectedMidi.store (-1.0f, std::memory_order_relaxed);
        targetMidi.store (-1.0f, std::memory_order_relaxed);
    }
    appliedShift = juce::jlimit (-7.0f, 7.0f, appliedShift);
    shiftCents.store (appliedShift * 100.0f, std::memory_order_relaxed);
    wasVoiced = voiced;

    auto& fr = frames[(size_t) (frameCount % frameCap)];
    fr.time = detector.getCentreTime();
    fr.f0 = voiced ? f0 : 0.0f;
    fr.shift = appliedShift;
    ++frameCount;
}

bool Tuner::framesAround (int64_t time, Frame& before, Frame& after) const noexcept
{
    if (frameCount == 0)
        return false;
    const int64_t oldest = juce::jmax ((int64_t) 0, frameCount - frameCap);
    for (int64_t i = frameCount - 1; i >= oldest; --i)
    {
        const auto& f = frames[(size_t) (i % frameCap)];
        if (f.time <= time || i == oldest)
        {
            before = f;
            after = i + 1 < frameCount ? frames[(size_t) ((i + 1) % frameCap)] : f;
            return true;
        }
    }
    return false;
}

float Tuner::periodAt (int64_t time, bool& voiced) const noexcept
{
    Frame a, b;
    voiced = false;
    if (! framesAround (time, a, b))
        return (float) pUnvoiced;
    const Frame& f = (time - a.time) <= (b.time - time) ? a : b;   // nearest frame decides voicing
    voiced = f.f0 > 0.0f;
    if (! voiced)
        return (float) pUnvoiced;
    float f0 = f.f0;
    if (a.f0 > 0.0f && b.f0 > 0.0f && b.time > a.time)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (float) (time - a.time) / (float) (b.time - a.time));
        f0 = a.f0 * std::pow (b.f0 / a.f0, u);
    }
    return juce::jlimit ((float) pMin, (float) pMax, (float) (fs / f0));
}

float Tuner::shiftAt (int64_t time) const noexcept
{
    Frame a, b;
    if (! framesAround (time, a, b))
        return 0.0f;
    if (b.time <= a.time)
        return a.shift;
    const float u = juce::jlimit (0.0f, 1.0f, (float) (time - a.time) / (float) (b.time - a.time));
    return a.shift + u * (b.shift - a.shift);
}

//==============================================================================
void Tuner::placeAnalysisMarks() noexcept
{
    while (true)
    {
        bool voiced = false;
        const float P = periodAt (nextPredicted, voiced);
        const int half = voiced ? (int) (P * 0.25f) : 0;
        if (nextPredicted + half + 2 >= t - detectLag)
            return;   // wait until both the audio and its pitch reading are in

        int64_t pos = nextPredicted;
        if (voiced)
        {
            // snap to the waveform peak (glottal pulse) so grains stay phase-consistent
            const int64_t lastPos = markCount > 0 ? mark (markCount - 1).pos : -(int64_t) pMax;
            const int64_t lo = juce::jmax (nextPredicted - half, lastPos + (int64_t) (0.6f * P));
            const int64_t hi = nextPredicted + half;
            float best = -1.0e9f;
            for (int64_t q = lo; q <= hi; ++q)
            {
                const float v = peakRing[(size_t) (q & mask)];
                if (v > best) { best = v; pos = q; }
            }
        }
        auto& m = marks[(size_t) (markCount & (markCap - 1))];
        m.pos = pos;
        m.voiced = voiced;
        ++markCount;
        bool v2 = false;
        const float nextP = periodAt (pos, v2);
        nextPredicted = pos + juce::jmax ((int64_t) 1, (int64_t) std::lround (voiced ? nextP : (float) pUnvoiced));
    }
}

void Tuner::placeSynthesisMarks (int numCh) noexcept
{
    if (markCount < 3)
        return;
    if (! synthStarted)
    {
        kCursor = 1;
        sNext = (double) mark (1).pos;
        synthStarted = true;
    }

    const int64_t outputPos = t - latency;
    while (true)
    {
        const auto s = (int64_t) std::floor (sNext);
        // analysis mark nearest to s
        while (kCursor + 1 < markCount && mark (kCursor + 1).pos <= s)
            ++kCursor;
        int64_t k = kCursor;
        if (k + 1 < markCount && (mark (k + 1).pos - s) < (s - mark (k).pos))
            k = k + 1;
        if (k + 1 >= markCount || k < 1)
            return;   // need the next mark before this grain can be built

        const auto& a = mark (k);
        const int left = (int) juce::jlimit ((int64_t) 1, (int64_t) pMax, a.pos - mark (k - 1).pos);
        const int right = (int) juce::jlimit ((int64_t) 1, (int64_t) pMax, mark (k + 1).pos - a.pos);

        if (s - left <= outputPos)
        {
            // too late to write this grain fully (only after a reset/start-up): skip it
            sNext += right;
            continue;
        }

        // overlap-add the asymmetric grain centred on s
        for (int j = -left; j < right; ++j)
        {
            const float w = j < 0 ? hannRise ((float) (j + left) / (float) left)
                                   : 1.0f - hannRise ((float) j / (float) right);
            const auto src = (size_t) ((a.pos + j) & mask);
            const auto dst = (size_t) ((s + j) & mask);
            for (int c = 0; c < numCh; ++c)
                yRing[(size_t) c][dst] += xRing[(size_t) c][src] * w;
            wRing[dst] += w;
        }

        const float shift = a.voiced ? shiftAt (a.pos) : 0.0f;
        if (std::abs (shift) < 0.0005f && ! a.voiced)
        {
            sNext = (double) mark (k + 1).pos;   // re-sync with the input in unvoiced sound
        }
        else
        {
            const double ratio = std::pow (2.0, shift / 12.0);
            sNext += juce::jmax (1.0, (double) right / ratio);
        }
    }
}

//==============================================================================
void Tuner::process (float* const* ch, int numCh, int n, const Params& p) noexcept
{
    nch = juce::jmin (numCh, maxChannels);
    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            xRing[(size_t) c][(size_t) (t & mask)] = ch[c][i];
            mono += ch[c][i];
        }
        mono /= (float) nch;
        peakRing[(size_t) (t & mask)] = sPeak.process (cPeak, mono);
        ++t;

        if (detector.push (mono))
            onDetection (p);

        placeAnalysisMarks();
        placeSynthesisMarks (nch);

        const int64_t o = t - 1 - latency;
        if (o < 0)
        {
            for (int c = 0; c < nch; ++c)
                ch[c][i] = 0.0f;
            continue;
        }
        const auto oi = (size_t) (o & mask);
        const float w = wRing[oi];
        const float a = juce::jlimit (0.0f, 1.0f, w / 0.3f);
        for (int c = 0; c < nch; ++c)
        {
            const float synth = yRing[(size_t) c][oi] / juce::jmax (w, 1.0e-6f);
            const float dry = xRing[(size_t) c][oi];
            ch[c][i] = a * synth + (1.0f - a) * dry;
            yRing[(size_t) c][oi] = 0.0f;
        }
        wRing[oi] = 0.0f;
    }
}

} // namespace oju
