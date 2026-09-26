#pragma once

#include "VocalChain.h"
#include "modules/PlosiveTamer.h"
#include "modules/Limiter.h"

namespace oju
{
// Settings for the OJU 2.0 modules that live outside the v1 chain.
struct EngineSettings
{
    ChainSettings chain;              // v1 chain (+ its 2.0 insertions)

    bool  plosiveOn = false;  float plosiveAmount = 0.5f;
    bool  limiterOn = false;  float limiterCeilingDb = -1.0f;
};

/*  OJU 2.0 signal flow

      [Denoise] -> [Plosive] -> [Tune] -> | v1 chain: Input -> Low cut -> EQ -> Tame -> (Leveler) -> Press
                                          |           -> Heat -> (Breath, Double) -> Space -> Amount -> Output |
                                          -> [Limiter]

    With every 2.0 module off, the engine calls the v1 chain exactly as v1 did, so the
    output is bit-identical to OJU v1 (checked by Tests/null_test.sh).                        */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void process (juce::AudioBuffer<float>& buffer, const EngineSettings& s) noexcept;

    int latencyFor (const EngineSettings& s) const noexcept;

    VocalChain& getChain() noexcept { return chain; }

    std::atomic<float> plosiveDb { 0.0f }, limiterGrDb { 0.0f };

private:
    void processChunk (float* const* ch, int nch, int n, const EngineSettings& s) noexcept;

    VocalChain chain;
    PlosiveTamer plosive;
    Limiter limiter;

    double fs = 48000.0;
    int maxBlock = 512, channels = 2;
    bool prepared = false;

    juce::AudioBuffer<float> chunkBuffer, rawCopy;
    bool lastPlosive = false, lastLimiter = false;
};

} // namespace oju
