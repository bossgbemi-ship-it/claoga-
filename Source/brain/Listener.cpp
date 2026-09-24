#include "Listener.h"
#include <algorithm>

namespace oju
{
namespace
{
    constexpr double maxCaptureSeconds = 60.0;
    constexpr double minUsefulSeconds = 1.5;

    float percentile (std::vector<float> v, float p)
    {
        if (v.empty())
            return -100.0f;
        std::sort (v.begin(), v.end());
        const auto idx = std::min (v.size() - 1, (size_t) std::round (p * (float) (v.size() - 1)));
        return v[idx];
    }

    // Balanced modern vocal long-term spectrum (dB, normalised to ~0 across 200 Hz - 2 kHz)
    float referenceDb (double f)
    {
        static const std::pair<double, float> pts[] = {
            { 20.0, -30.0f }, { 60.0, -16.0f }, { 100.0, -7.0f }, { 150.0, -2.5f }, { 250.0, 0.0f },
            { 500.0, 0.0f }, { 1000.0, -2.0f }, { 2000.0, -5.5f }, { 3000.0, -7.0f }, { 4000.0, -9.0f },
            { 6000.0, -12.0f }, { 8000.0, -15.0f }, { 10000.0, -18.0f }, { 12000.0, -21.0f },
            { 16000.0, -28.0f }, { 20000.0, -36.0f }, { 96000.0, -60.0f }
        };
        if (f <= pts[0].first)
            return pts[0].second;
        for (size_t i = 1; i < std::size (pts); ++i)
        {
            if (f <= pts[i].first)
            {
                const double t = std::log (f / pts[i - 1].first) / std::log (pts[i].first / pts[i - 1].first);
                return (float) (pts[i - 1].second + t * (pts[i].second - pts[i - 1].second));
            }
        }
        return -60.0f;
    }

    struct LogSpectrum
    {
        std::vector<double> freq;
        std::vector<float> db;       // smoothed, normalised
        std::vector<float> excess;   // db - reference

        float valueAt (const std::vector<float>& v, double f) const
        {
            if (freq.empty())
                return 0.0f;
            if (f <= freq.front()) return v.front();
            if (f >= freq.back())  return v.back();
            auto it = std::lower_bound (freq.begin(), freq.end(), f);
            const auto i = (size_t) std::distance (freq.begin(), it);
            const double t = std::log (f / freq[i - 1]) / std::log (freq[i] / freq[i - 1]);
            return (float) (v[i - 1] + t * (v[i] - v[i - 1]));
        }

        float mean (const std::vector<float>& v, double lo, double hi) const
        {
            double sum = 0.0; int n = 0;
            for (size_t i = 0; i < freq.size(); ++i)
                if (freq[i] >= lo && freq[i] <= hi) { sum += v[i]; ++n; }
            return n > 0 ? (float) (sum / n) : valueAt (v, std::sqrt (lo * hi));
        }

        std::pair<double, float> peak (const std::vector<float>& v, double lo, double hi) const
        {
            std::pair<double, float> best { std::sqrt (lo * hi), -1000.0f };
            for (size_t i = 0; i < freq.size(); ++i)
                if (freq[i] >= lo && freq[i] <= hi && v[i] > best.second)
                    best = { freq[i], v[i] };
            if (best.second < -999.0f)
                best.second = valueAt (v, best.first);
            return best;
        }
    };

    // 1/6-octave smoothing on a 1/24-octave grid, normalised so 200 Hz - 2 kHz averages 0 dB.
    LogSpectrum makeLogSpectrum (const std::vector<double>& power, double binHz, double nyquist)
    {
        LogSpectrum s;
        const double top = juce::jmin (20000.0, nyquist * 0.95);
        for (double f = 20.0; f <= top; f *= std::pow (2.0, 1.0 / 24.0))
        {
            const double lo = f * std::pow (2.0, -1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
            auto b0 = (size_t) std::ceil (lo / binHz), b1 = (size_t) std::floor (hi / binHz);
            b1 = std::min (b1, power.size() - 1);
            double sum = 0.0; int n = 0;
            for (auto b = std::max<size_t> (1, b0); b <= b1; ++b) { sum += power[b]; ++n; }
            if (n == 0)
            {
                const double pos = f / binHz;
                const auto i0 = std::clamp<size_t> ((size_t) pos, 1, power.size() - 2);
                const double t = juce::jlimit (0.0, 1.0, pos - (double) i0);
                sum = power[i0] + t * (power[i0 + 1] - power[i0]);
                n = 1;
            }
            s.freq.push_back (f);
            s.db.push_back ((float) (10.0 * std::log10 (sum / n + 1.0e-20)));
        }

        double norm = 0.0; int count = 0;
        for (size_t i = 0; i < s.freq.size(); ++i)
            if (s.freq[i] >= 200.0 && s.freq[i] <= 2000.0) { norm += s.db[i]; ++count; }
        norm = count > 0 ? norm / count : 0.0;

        for (size_t i = 0; i < s.freq.size(); ++i)
        {
            s.db[i] -= (float) norm;
            s.excess.push_back (s.db[i] - referenceDb (s.freq[i]));
        }
        return s;
    }
}

//==============================================================================
Listener::Listener() : juce::Thread ("OJU listener")
{
    fifoData.resize ((size_t) fifoSize, 0.0f);
    startThread (juce::Thread::Priority::low);
}

Listener::~Listener()
{
    capturing.store (false);
    stopThread (3000);
}

void Listener::pushAudio (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! capturing.load (std::memory_order_acquire) || numChannels <= 0)
        return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    const float scale = 1.0f / (float) numChannels;

    auto write = [&] (int dest, int count, int srcOffset)
    {
        for (int i = 0; i < count; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                s += channels[c][srcOffset + i];
            fifoData[(size_t) (dest + i)] = s * scale;
        }
    };
    write (start1, size1, 0);
    write (start2, size2, size1);
    fifo.finishedWrite (size1 + size2);   // if full, the excess is simply dropped
}

void Listener::begin (int mode)
{
    requestedMode.store (mode);
    startRequested.store (true);
    progress.store (0.0f);
    state.store ((int) State::listening);
    notify();
}

void Listener::cancel()
{
    capturing.store (false);
    cancelRequested.store (true);
    notify();
}

bool Listener::takeResult (Features& out)
{
    const juce::ScopedLock sl (resultLock);
    if (! resultReady)
        return false;
    out = result;
    resultReady = false;
    return true;
}

void Listener::drainInto (bool keep)
{
    int start1, size1, start2, size2;
    const int ready = fifo.getNumReady();
    fifo.prepareToRead (ready, start1, size1, start2, size2);
    if (keep)
    {
        capture.insert (capture.end(), fifoData.begin() + start1, fifoData.begin() + start1 + size1);
        capture.insert (capture.end(), fifoData.begin() + start2, fifoData.begin() + start2 + size2);
    }
    fifo.finishedRead (size1 + size2);
}

void Listener::startCapture()
{
    capturing.store (false, std::memory_order_release);
    drainInto (false);

    captureMode = requestedMode.load();
    captureRate = sampleRate.load();
    frameLength = juce::jmax (64, (int) std::round (captureRate * 0.02));
    framesDone = voicedFrames = 0;
    noiseFloorDb = -60.0f;

    capture.clear();
    capture.reserve ((size_t) (captureRate * 30.0));
    voiced.clear();
    frameDb.clear();

    progress.store (0.0f);
    state.store ((int) State::listening);
    capturing.store (true, std::memory_order_release);
}

void Listener::updateVoiceGate()
{
    // Energy gate with an adaptive noise floor: a 20 ms frame counts as "singing"
    // when it is clearly above the floor and above an absolute minimum.
    const float riseDbPerFrame = 4.0f * 0.02f;
    while ((size_t) ((framesDone + 1) * frameLength) <= capture.size())
    {
        const float* p = capture.data() + (size_t) framesDone * (size_t) frameLength;
        double sum = 0.0;
        for (int i = 0; i < frameLength; ++i)
            sum += (double) p[i] * p[i];
        const auto db = (float) (10.0 * std::log10 (sum / frameLength + 1.0e-12));

        noiseFloorDb = db < noiseFloorDb ? db : juce::jmin (noiseFloorDb + riseDbPerFrame, db);
        const bool isVoice = db > -52.0f && db > noiseFloorDb + 10.0f;

        voiced.push_back (isVoice ? 1 : 0);
        frameDb.push_back (db);
        voicedFrames += isVoice ? 1 : 0;
        ++framesDone;
    }
}

void Listener::run()
{
    while (! threadShouldExit())
    {
        if (cancelRequested.exchange (false))
        {
            capturing.store (false);
            drainInto (false);
            state.store ((int) State::idle);
            progress.store (0.0f);
        }

        if (startRequested.exchange (false))
            startCapture();

        if (capturing.load() && getState() == State::listening)
        {
            drainInto (true);
            updateVoiceGate();

            const double target = captureMode == 1 ? extremeSeconds : naturalSeconds;
            const double voicedSeconds = voicedFrames * frameLength / captureRate;
            progress.store ((float) juce::jmin (1.0, voicedSeconds / target));

            const bool enough = voicedSeconds >= target;
            const bool timedOut = (double) capture.size() / captureRate >= maxCaptureSeconds;

            if (enough || timedOut)
            {
                capturing.store (false);

                if (voicedSeconds < minUsefulSeconds)
                {
                    state.store ((int) State::failed);
                }
                else
                {
                    state.store ((int) State::analysing);
                    auto f = analyse (capture, voiced, frameLength, captureRate, captureMode);
                    {
                        const juce::ScopedLock sl (resultLock);
                        result = f;
                        resultReady = true;
                    }
                    state.store ((int) (f.valid ? State::done : State::failed));
                }
                capture.clear();
                capture.shrink_to_fit();
            }
        }

        wait (capturing.load() ? 15 : 60);
    }
}

//==============================================================================
Features Listener::analyse (const std::vector<float>& audio, const std::vector<char>& voicedMask,
                            int frameLen, double sr, int mode)
{
    Features f;
    f.mode = mode;
    f.sampleRate = (float) sr;

    // ---- Level statistics over voiced 20 ms frames
    std::vector<float> rms;
    double powerSum = 0.0;
    float peak = 0.0f;
    for (size_t fr = 0; fr < voicedMask.size(); ++fr)
    {
        if (! voicedMask[fr])
            continue;
        const float* p = audio.data() + fr * (size_t) frameLen;
        double sum = 0.0;
        for (int i = 0; i < frameLen; ++i)
        {
            sum += (double) p[i] * p[i];
            peak = juce::jmax (peak, std::abs (p[i]));
        }
        powerSum += sum / frameLen;
        rms.push_back ((float) (10.0 * std::log10 (sum / frameLen + 1.0e-12)));
    }

    if (rms.size() < 20)
        return f;

    f.voicedSeconds = (float) ((double) (rms.size() * (size_t) frameLen) / sr);
    f.rmsP10 = percentile (rms, 0.10f);
    f.rmsP50 = percentile (rms, 0.50f);
    f.rmsP90 = percentile (rms, 0.90f);
    f.rmsP95 = percentile (rms, 0.95f);
    f.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    f.crestDb = f.peakDb - (float) (10.0 * std::log10 (powerSum / (double) rms.size() + 1.0e-12));
    f.dynamicsDb = f.rmsP95 - f.rmsP10;

    // ---- Spectral analysis of voiced FFT frames
    int order = mode == 1 ? 13 : 11;
    if (sr > 60000.0)  ++order;
    if (sr > 120000.0) ++order;
    const int n = 1 << order;
    const int hop = mode == 1 ? n / 4 : n / 2;
    const int bins = n / 2 + 1;
    const double binHz = sr / n;

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) n, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> work ((size_t) n * 2);

    std::vector<double> avg ((size_t) bins, 0.0);
    std::vector<float> frameSpectra;     // per-frame magnitudes, for the tonal / sibilant split
    std::vector<float> frameSibRatio;
    int frames = 0;

    auto bandBins = [&] (double lo, double hi)
    {
        return std::make_pair (juce::jmax (1, (int) std::ceil (lo / binHz)),
                               juce::jmin (bins - 1, (int) std::floor (hi / binHz)));
    };
    const auto sib = bandBins (4500.0, juce::jmin (11000.0, sr * 0.45));
    const auto all = bandBins (100.0, juce::jmin (16000.0, sr * 0.45));

    for (size_t start = 0; start + (size_t) n <= audio.size(); start += (size_t) hop)
    {
        const size_t f0 = start / (size_t) frameLen;
        const size_t f1 = std::min (voicedMask.size(), (start + (size_t) n) / (size_t) frameLen);
        int v = 0, total = 0;
        for (size_t k = f0; k < f1; ++k) { v += voicedMask[k]; ++total; }
        if (total == 0 || v * 10 < total * 6)
            continue;

        std::fill (work.begin(), work.end(), 0.0f);
        std::copy (audio.begin() + (long) start, audio.begin() + (long) start + n, work.begin());
        window.multiplyWithWindowingTable (work.data(), (size_t) n);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        double sibE = 0.0, allE = 0.0;
        for (int b = 0; b < bins; ++b)
        {
            const double pw = (double) work[(size_t) b] * work[(size_t) b];
            avg[(size_t) b] += pw;
            if (b >= sib.first && b <= sib.second) sibE += pw;
            if (b >= all.first && b <= all.second) allE += pw;
        }
        frameSibRatio.push_back ((float) (10.0 * std::log10 ((sibE + 1.0e-20) / (allE + 1.0e-20))));
        frameSpectra.insert (frameSpectra.end(), work.begin(), work.begin() + bins);
        ++frames;
    }

    if (frames < 3)
        return f;

    for (auto& a : avg)
        a /= frames;

    const double nyquist = sr * 0.5;

    // Tone is read from the non-sibilant frames only, so S's don't masquerade as air or presence.
    std::vector<double> tonal ((size_t) bins, 0.0);
    {
        const float p80 = percentile (frameSibRatio, 0.80f);
        int count = 0;
        for (size_t fr = 0; fr < frameSibRatio.size(); ++fr)
        {
            if (frameSibRatio[fr] > p80)
                continue;
            const float* sp = frameSpectra.data() + fr * (size_t) bins;
            for (int b = 0; b < bins; ++b)
                tonal[(size_t) b] += (double) sp[b] * sp[b];
            ++count;
        }
        if (count == 0)
            tonal = avg;
        else
            for (auto& v : tonal)
                v /= count;
    }

    const auto full = makeLogSpectrum (avg, binHz, nyquist);
    const auto spec = makeLogSpectrum (tonal, binHz, nyquist);
    const auto& E = spec.excess;
    const auto& S = spec.db;

    // Local resonance: excess relative to its own one-octave neighbourhood.
    std::vector<float> bump (E.size(), 0.0f);
    for (size_t i = 0; i < E.size(); ++i)
    {
        // least-squares line through +/- half an octave (in log frequency), so a sloping
        // spectrum around the fundamentals doesn't read as a resonance
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0; int cnt = 0;
        for (size_t j = 0; j < E.size(); ++j)
        {
            const double x = std::log2 (spec.freq[j] / spec.freq[i]);
            if (std::abs (x) <= 0.5)
            {
                sx += x; sy += E[j]; sxx += x * x; sxy += x * E[j]; ++cnt;
            }
        }
        const double denom = cnt * sxx - sx * sx;
        const double trendAtCentre = std::abs (denom) > 1.0e-9 ? (sy * sxx - sx * sxy) / denom : sy / juce::jmax (1, cnt);
        bump[i] = E[i] - (float) trendAtCentre;
    }

    const float baseline = spec.mean (E, 150.0, 3000.0);
    f.rumbleDb = spec.mean (E, 25.0, 70.0) - baseline;

    // Lowest part of the voice: first point within 15 dB of the strongest low-mid content
    {
        const float strongest = spec.peak (S, 80.0, 1000.0).second;
        f.lowestVoiceHz = 500.0f;
        for (size_t i = 0; i < spec.freq.size(); ++i)
            if (spec.freq[i] >= 60.0 && spec.freq[i] <= 500.0 && S[i] > strongest - 15.0f)
            {
                f.lowestVoiceHz = (float) spec.freq[i];
                break;
            }
    }

    {
        // How much: broad build-up of the band. Where: its most resonant point.
        f.mudFreq = (float) spec.peak (bump, 180.0, 550.0).first;
        f.mudExcessDb = spec.mean (E, 200.0, 500.0) - baseline + 0.5f * juce::jmax (0.0f, spec.valueAt (bump, f.mudFreq));

        f.boxFreq = (float) spec.peak (bump, 550.0, 1100.0).first;
        f.boxExcessDb = spec.mean (E, 550.0, 1000.0) - baseline + 0.5f * juce::jmax (0.0f, spec.valueAt (bump, f.boxFreq));
    }

    f.presenceDb = spec.mean (E, 2000.0, 5000.0) - spec.mean (E, 200.0, 1500.0);

    {
        const auto harsh = spec.peak (E, 2500.0, 5000.0);
        f.harshFreq = (float) harsh.first;
        f.harshExcessDb = harsh.second - spec.mean (E, 1800.0, 6500.0);
    }

    const float midRef = spec.mean (E, 1000.0, 4000.0);
    const double sibTop = juce::jmin (11000.0, nyquist * 0.9);

    if (mode == 1 && ! frameSibRatio.empty())
    {
        // Extreme: percentile-based. Average only the frames in the top 10% of
        // sibilant energy, then find where those S's actually peak.
        const float p90 = percentile (frameSibRatio, 0.90f);
        const float p95 = percentile (frameSibRatio, 0.95f);
        std::vector<double> sibAvg ((size_t) bins, 0.0);
        int count = 0;
        for (size_t fr = 0; fr < frameSibRatio.size(); ++fr)
        {
            if (frameSibRatio[fr] < p90)
                continue;
            const float* sp = frameSpectra.data() + fr * (size_t) bins;
            for (int b = 0; b < bins; ++b)
                sibAvg[(size_t) b] += (double) sp[b] * sp[b];
            ++count;
        }
        if (count > 0)
        {
            const auto sibSpec = makeLogSpectrum (sibAvg, binHz, nyquist);
            f.sibilanceFreq = (float) sibSpec.peak (sibSpec.db, 4500.0, sibTop).first;
        }
        else
        {
            f.sibilanceFreq = (float) full.peak (full.excess, 4500.0, sibTop).first;
        }
        f.sibilanceDb = juce::jlimit (-10.0f, 15.0f, p95 + 10.0f);
    }
    else
    {
        const auto s = full.peak (full.excess, 4500.0, sibTop);
        f.sibilanceFreq = (float) s.first;
        f.sibilanceDb = juce::jlimit (-10.0f, 15.0f, s.second - full.mean (full.excess, 1000.0, 4000.0));
    }

    f.airDb = nyquist > 11000.0 ? spec.mean (E, 10000.0, juce::jmin (15000.0, nyquist * 0.9)) - midRef : 0.0f;

    for (int i = 0; i < Features::spectrumPoints; ++i)
    {
        const double fr = 20.0 * std::pow (1000.0, (double) i / (Features::spectrumPoints - 1));
        f.spectrumDb[(size_t) i] = fr < nyquist ? spec.valueAt (S, fr) : -60.0f;
    }

    f.valid = true;
    return f;
}

} // namespace oju
