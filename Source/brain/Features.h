#pragma once

#include <array>

namespace oju
{
// What the brain measured from the captured vocal. Plain data: copied between
// threads by value and stored in the plugin state so styles can be re-applied
// later without listening again.
struct Features
{
    static constexpr int spectrumPoints = 64;   // display spectrum, 20 Hz .. 20 kHz, log spaced

    bool  valid = false;
    int   mode = 0;                 // 0 natural, 1 extreme
    float sampleRate = 48000.0f;
    float voicedSeconds = 0.0f;

    // Level / dynamics (dBFS, raw input, voiced frames only)
    float rmsP10 = -40.0f, rmsP50 = -30.0f, rmsP90 = -24.0f, rmsP95 = -22.0f;
    float peakDb = -12.0f;
    float crestDb = 15.0f;          // peak vs mean RMS
    float dynamicsDb = 12.0f;       // p95 - p10

    // Tone (dB relative to a balanced reference vocal curve)
    float rumbleDb = -10.0f;
    float lowestVoiceHz = 120.0f;
    float mudFreq = 300.0f,  mudExcessDb = 0.0f;
    float boxFreq = 700.0f,  boxExcessDb = 0.0f;
    float presenceDb = 0.0f;        // 2-5 kHz vs 200-1500 Hz, relative to reference
    float harshFreq = 3500.0f, harshExcessDb = 0.0f;
    float sibilanceFreq = 7000.0f, sibilanceDb = 0.0f;   // severity (dB, higher = sharper S's)
    float airDb = 0.0f;

    std::array<float, spectrumPoints> spectrumDb {};

    // OJU 2.0: background noise (quiet moments between phrases)
    float noiseFloorDb = -90.0f;

    // OJU 2.0: the key of the sung melody (pitch-class histogram vs key profiles)
    int   keyRoot = -1;          // 0 = C .. 11 = B, -1 unknown
    bool  keyMinor = true;
    float keyConfidence = 0.0f;  // 0..1
};

} // namespace oju
