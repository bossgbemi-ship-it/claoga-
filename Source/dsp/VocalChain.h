#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include "Svf.h"

namespace oju
{
// One block's worth of parameter targets (read from the APVTS atomics).
struct ChainSettings
{
    bool  bypass = false;
    float inputGainDb = 0.0f, outputGainDb = 0.0f, amount = 1.0f;
    bool  ceilingOn = true;

    bool  lowCutOn = true;  float lowCutFreq = 80.0f;
    bool  eqOn = true;
    float bodyGain = 0.0f, mudFreq = 300.0f, mudGain = 0.0f;
    float presenceFreq = 3500.0f, presenceGain = 0.0f, airGain = 0.0f;

    bool  tameOn = true;  float tameFreq = 6500.0f, tameAmount = 0.3f;

    bool  pressOn = true;
    float threshold = -18.0f, ratio = 3.0f, attackMs = 10.0f, releaseMs = 120.0f, makeupDb = 2.0f;
    float parallel = 0.0f;
    bool  extreme = false;

    bool  heatOn = true;  float heatDriveDb = 4.0f, heatMix = 0.35f;

    bool  spaceOn = true;
    int   delayDivision = 3;
    float feedback = 0.25f, delayMix = 0.1f, reverbSize = 0.45f, reverbMix = 0.12f;
    double bpm = 120.0;
};

// Fixed voicing of the Shape EQ.
namespace shape
{
    constexpr double bodyFreq = 160.0,  bodyQ = 0.7;
    constexpr double mudQ = 1.4;
    constexpr double presenceQ = 0.9;
    constexpr double airFreq = 10000.0, airQ = 0.7;
    constexpr double lowCutQ1 = 0.5412, lowCutQ2 = 1.3066; // 24 dB/oct Butterworth
}

// Magnitude (dB) of the Shape section (low cut + EQ) at a frequency. Used by the UI curve.
float shapeResponseDb (const ChainSettings& s, double freq, double sampleRate);

double delaySecondsFor (int division, double bpm);

class VocalChain
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    int getLatencySamples() const noexcept { return latency; }

    // The latency is fixed by the Heat oversampler design, so it can be reported before prepare().
    static int expectedLatencySamples();

    // In-place. Must be called with <= maxChannels channels. Real-time safe.
    void process (juce::AudioBuffer<float>& buffer, const ChainSettings& s) noexcept;

    // Meters (written by the audio thread, consumed/cleared by the UI).
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, pressGrDb { 0.0f }, tameGrDb { 0.0f };

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int n, const ChainSettings& s) noexcept;
    void setTargets (const ChainSettings& s) noexcept;
    void updateShapeCoeffs() noexcept;
    void updateDynamicsCoeffs() noexcept;

    using Lin = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    using Mul = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

    double fs = 44100.0;
    int maxBlock = 512, channels = 2, latency = 0;
    bool prepared = false;
    int controlCountdown = 0;
    static constexpr int controlInterval = 16;

    // --- smoothed parameters -------------------------------------------------
    Lin inGain, outGain, amountMix, bypassMix, ceilingMix;
    Lin lowCutMix, eqMix, tameMix, pressMix, heatMix, spaceMix;
    Mul lowCutFreq, mudFreq, presenceFreq, tameFreq;
    Lin bodyGain, mudGain, presenceGain, airGain;
    Lin tameAmount, makeup, parallel;
    Lin heatDrive, heatWet;      // running at the oversampled rate
    Lin echoLevel, feedback, verbLevel;
    Lin delaySamples;

    float thresholdDb = -18.0f, ratio = 3.0f, attackMs = 10.0f, releaseMs = 120.0f;
    float reverbSizeTarget = -1.0f;

    // --- Shape ---------------------------------------------------------------
    SvfCoeffs cLowCut1, cLowCut2, cBody, cMud, cPresence, cAir;
    std::array<SvfState, maxChannels> sLowCut1, sLowCut2, sBody, sMud, sPresence, sAir;

    // --- Tame ----------------------------------------------------------------
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    float tameLastFreq = 0.0f;
    float tameEnvHi = 0.0f, tameEnvFull = 0.0f, tameRedDb = 0.0f;
    float tameEnvAtt = 0.0f, tameEnvRel = 0.0f, tameGainAtt = 0.0f, tameGainRel = 0.0f;

    // --- Press ---------------------------------------------------------------
    struct CompState { float y1 = 0.0f, yl = 0.0f; };
    CompState comp, crush;
    float compAttCoef = 0.0f, compRelCoef = 0.0f, crushAttCoef = 0.0f, crushRelCoef = 0.0f;

    // --- Heat ----------------------------------------------------------------
    static std::unique_ptr<juce::dsp::Oversampling<float>> makeOversampler (int numChannels);
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<float, maxChannels> heatLp {}, dcX1 {}, dcY1 {};
    float heatLpCoef = 0.0f, dcR = 0.999f;

    // --- Space ---------------------------------------------------------------
    juce::AudioBuffer<float> delayBuffer, reverbBuffer;
    int delayWrite = 0, delayLength = 0;
    std::array<float, maxChannels> fbHp {}, fbHpX1 {}, fbLp {}, verbHp {}, verbHpX1 {};
    float fbHpCoef = 0.0f, fbLpCoef = 0.0f, verbHpCoef = 0.0f;
    juce::Reverb reverb;
    bool reverbActive = false;

    // --- Latency-compensated dry paths --------------------------------------
    juce::AudioBuffer<float> dryRing, rawRing;
    int ringWrite = 0, ringSize = 1;

    int lastDivision = -1;
    double lastBpm = 0.0;
};

} // namespace oju
