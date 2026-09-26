#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>
#include "PitchDetector.h"

namespace oju
{
/*  Tune: pitch correction (our own code, no third-party pitch code).

    Detection:  YIN (PitchDetector) every ~5 ms on the mono vocal.
    Correction: the sung pitch is snapped to the nearest note of the key/scale, with
                hysteresis so it never flutters between two notes. Retune speed goes from
                Natural (a slow, human glide) to Hard (an instant, robotic snap). Humanize
                measures the shift from the note's centre instead of every wobble, so
                vibrato and expression survive.
    Shifting:   TD-PSOLA with pitch-synchronous, asymmetric grains. Each grain is one
                glottal pulse and is re-spaced, not resampled, so formants stay put (no
                chipmunk). With no correction needed, the grains reassemble the input
                exactly.
    Latency:    pitch-detection time + 3 periods of the lowest note (about 64 ms).
                Mix mode only; Tune rests in Track mode.                                */
class Tuner
{
public:
    static constexpr int maxChannels = 2;

    struct Params
    {
        int key = 0;            // 0 = C .. 11 = B
        int scale = 1;          // Scale enum
        float speed = 0.4f;     // 0 natural .. 1 hard
        float humanize = 0.5f;  // 0 .. 1
        float amount = 1.0f;    // 0 .. 1
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latencySamples() const noexcept { return latency; }

    void process (float* const* ch, int nch, int n, const Params& p) noexcept;

    // UI readouts
    std::atomic<float> detectedMidi { -1.0f }, targetMidi { -1.0f }, shiftCents { 0.0f };

    static bool noteInScale (int pitchClass, int key, int scale) noexcept;

private:
    struct Frame { int64_t time = 0; float f0 = 0.0f; float shift = 0.0f; };
    struct Mark  { int64_t pos = 0; bool voiced = false; };

    void onDetection (const Params& p) noexcept;
    float periodAt (int64_t time, bool& voiced) const noexcept;
    float shiftAt (int64_t time) const noexcept;
    bool framesAround (int64_t time, Frame& before, Frame& after) const noexcept;
    void placeAnalysisMarks() noexcept;
    void placeSynthesisMarks (int nch) noexcept;
    const Mark& mark (int64_t index) const noexcept { return marks[(size_t) (index & (markCap - 1))]; }

    double fs = 48000.0;
    int latency = 2400, pMax = 686, pMin = 44, pUnvoiced = 240, detectLag = 1000;
    int ringSize = 8192;
    int64_t mask = 8191;

    std::array<std::vector<float>, maxChannels> xRing, yRing;
    std::vector<float> wRing, peakRing;
    int64_t t = 0;

    PitchDetector detector;
    SvfCoeffs cPeak;
    SvfState sPeak;

    static constexpr int frameCap = 64;
    std::array<Frame, frameCap> frames {};
    int64_t frameCount = 0;

    static constexpr int markCap = 256;
    std::array<Mark, markCap> marks {};
    int64_t markCount = 0;
    int64_t nextPredicted = 0;

    double sNext = 0.0;
    int64_t kCursor = 0;
    bool synthStarted = false;

    // corrector state
    float centreMidi = 0.0f, appliedShift = 0.0f, currentTarget = -100.0f;
    bool wasVoiced = false;
    int nch = 2;
};

} // namespace oju
