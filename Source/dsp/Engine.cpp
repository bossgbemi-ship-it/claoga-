#include "Engine.h"

namespace oju
{
namespace
{
    void storeMax (std::atomic<float>& a, float v) noexcept
    {
        if (v > a.load (std::memory_order_relaxed))
            a.store (v, std::memory_order_relaxed);
    }
}

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    maxBlock = juce::jmax (32, maxBlockSize);   // same chunking as the v1 chain
    channels = juce::jlimit (1, VocalChain::maxChannels, numChannels);

    chain.prepare (sampleRate, maxBlockSize, numChannels);
    plosive.prepare (fs, maxBlock, channels);
    limiter.prepare (fs, maxBlock);

    chunkBuffer.setSize (VocalChain::maxChannels, maxBlock);
    rawCopy.setSize (VocalChain::maxChannels, maxBlock);
    prepared = true;
}

void Engine::reset()
{
    chain.reset();
    plosive.reset();
    limiter.reset();
}

int Engine::latencyFor (const EngineSettings& s) const noexcept
{
    const bool track = s.chain.trackMode;
    int l = chain.getLatencySamples (track);
    if (s.limiterOn)
        l += limiter.latencySamples (track);
    return l;
}

void Engine::process (juce::AudioBuffer<float>& buffer, const EngineSettings& s) noexcept
{
    if (! prepared)
        return;

    const int nch = juce::jmin (buffer.getNumChannels(), channels);
    const int total = buffer.getNumSamples();
    if (nch <= 0 || total <= 0)
        return;

    // Same chunk boundaries the v1 chain uses internally, so v1 behaviour is unchanged.
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        float* ch[VocalChain::maxChannels] = { buffer.getWritePointer (0, start),
                                               nch > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (ch, nch, n, s);
    }
}

void Engine::processChunk (float* const* ch, int nch, int n, const EngineSettings& s) noexcept
{
    const bool pre = s.plosiveOn;

    // Start modules from a clean state when they are switched on.
    if (s.plosiveOn && ! lastPlosive) plosive.reset();
    if (s.limiterOn && ! lastLimiter) limiter.reset();
    lastPlosive = s.plosiveOn;
    lastLimiter = s.limiterOn;

    // Keep the untouched input for the chain's dry (Amount) and bypass paths.
    if (pre)
        for (int c = 0; c < nch; ++c)
            juce::FloatVectorOperations::copy (rawCopy.getWritePointer (c), ch[c], n);

    // ---- modules before the v1 chain
    if (s.plosiveOn)
        storeMax (plosiveDb, plosive.process (ch, nch, n, s.plosiveAmount));

    // ---- the v1 chain (with its 2.0 insertions)
    juce::AudioBuffer<float> view (const_cast<float**> (ch), nch, n);
    const float* raws[VocalChain::maxChannels] = { rawCopy.getReadPointer (0), rawCopy.getReadPointer (1) };
    chain.process (view, s.chain, pre ? raws : nullptr, nullptr);

    // ---- modules after the chain
    if (s.limiterOn)
        storeMax (limiterGrDb, limiter.process (ch, nch, n, s.limiterCeilingDb, s.chain.trackMode, s.chain.bypass));
}

} // namespace oju
