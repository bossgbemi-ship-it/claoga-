#include "VocalChain.h"

namespace oju
{
namespace
{
    constexpr float ceilingLin = 0.96605f;   // -0.3 dBFS
    constexpr float kneeDb = 6.0f;
    constexpr float heatAsym = 0.25f;

    inline float dbToGain (float db) noexcept   { return std::exp (db * 0.115129255f); }
    inline float gainToDb (float g) noexcept    { return 8.68588964f * std::log (g + 1.0e-9f); }
    inline float onePoleCoef (double seconds, double rate) { return (float) std::exp (-1.0 / (juce::jmax (1.0e-6, seconds) * rate)); }

    // Soft-knee gain computer (Giannoulis et al.), returns gain reduction in dB (>= 0)
    inline float computeReduction (float xg, float t, float r) noexcept
    {
        const float over = xg - t;
        if (2.0f * over < -kneeDb)
            return 0.0f;
        if (2.0f * std::abs (over) <= kneeDb)
        {
            const float z = over + kneeDb * 0.5f;
            return (1.0f - 1.0f / r) * z * z / (2.0f * kneeDb);
        }
        return over * (1.0f - 1.0f / r);
    }

    inline float softCeiling (float x) noexcept
    {
        constexpr float t = ceilingLin * 0.7f;
        constexpr float span = ceilingLin - t;
        const float a = std::abs (x);
        if (a <= t)
            return x;
        const float y = t + span * std::tanh ((a - t) / span);
        return x < 0.0f ? -y : y;
    }

    void storeMax (std::atomic<float>& a, float v) noexcept
    {
        if (v > a.load (std::memory_order_relaxed))
            a.store (v, std::memory_order_relaxed);
    }
}

double delaySecondsFor (int division, double bpm)
{
    const double beat = 60.0 / juce::jlimit (30.0, 300.0, bpm);
    switch (division)
    {
        case 0:  return 0.095;           // slap
        case 1:  return beat * 0.25;     // 1/16
        case 2:  return beat * 0.5;      // 1/8
        case 3:  return beat * 0.75;     // 1/8 dotted
        default: return beat;            // 1/4
    }
}

float shapeResponseDb (const ChainSettings& s, double freq, double fs)
{
    double mag = 1.0;
    if (s.lowCutOn)
    {
        mag *= svfMagnitude (makeSvf (SvfType::highpass, fs, s.lowCutFreq, shape::lowCutQ1), freq, fs);
        mag *= svfMagnitude (makeSvf (SvfType::highpass, fs, s.lowCutFreq, shape::lowCutQ2), freq, fs);
    }
    if (s.eqOn)
    {
        mag *= svfMagnitude (makeSvf (SvfType::lowShelf, fs, shape::bodyFreq, shape::bodyQ, s.bodyGain), freq, fs);
        mag *= svfMagnitude (makeSvf (SvfType::bell, fs, s.mudFreq, shape::mudQ, s.mudGain), freq, fs);
        mag *= svfMagnitude (makeSvf (SvfType::bell, fs, s.presenceFreq, shape::presenceQ, s.presenceGain), freq, fs);
        mag *= svfMagnitude (makeSvf (SvfType::highShelf, fs, shape::airFreq, shape::airQ, s.airGain), freq, fs);
    }
    return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag)));
}

//==============================================================================
std::unique_ptr<juce::dsp::Oversampling<float>> VocalChain::makeOversampler (int numChannels)
{
    // 2x, polyphase IIR, integer latency so the dry path can match it exactly.
    return std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
}

int VocalChain::expectedLatencySamples()
{
    static const int value = [] { auto os = makeOversampler (2); os->initProcessing (64); return juce::roundToInt (os->getLatencyInSamples()); }();
    return value;
}

void VocalChain::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    maxBlock = juce::jmax (32, maxBlockSize);
    channels = juce::jlimit (1, maxChannels, numChannels);

    oversampler = makeOversampler (channels);
    oversampler->initProcessing ((size_t) maxBlock);
    latency = juce::roundToInt (oversampler->getLatencyInSamples());

    crossover.prepare ({ fs, (juce::uint32) maxBlock, (juce::uint32) maxChannels });
    crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);

    delayLength = (int) std::ceil (fs * 3.0) + 4;
    delayBuffer.setSize (maxChannels, delayLength);
    reverbBuffer.setSize (maxChannels, maxBlock);

    ringSize = latency + maxBlock + 2;
    dryRing.setSize (maxChannels, ringSize);
    rawRing.setSize (maxChannels, ringSize);

    reverb.setSampleRate (fs);

    const double ramp = 0.03;
    for (auto* v : { &inGain, &outGain, &amountMix, &bypassMix, &ceilingMix, &lowCutMix, &eqMix, &tameMix,
                     &pressMix, &spaceMix, &bodyGain, &mudGain, &presenceGain, &airGain, &tameAmount,
                     &makeup, &parallel, &echoLevel, &feedback, &verbLevel })
        v->reset (fs, ramp);
    for (auto* v : { &lowCutFreq, &mudFreq, &presenceFreq, &tameFreq })
        v->reset (fs, 0.05);
    heatDrive.reset (fs * 2.0, ramp);
    heatWet.reset (fs * 2.0, ramp);
    heatMix.reset (fs * 2.0, ramp);
    delaySamples.reset (fs, 0.25);

    // Fixed filter constants
    tameEnvAtt  = onePoleCoef (0.0002, fs);
    tameEnvRel  = onePoleCoef (0.030, fs);
    tameGainAtt = onePoleCoef (0.0005, fs);
    tameGainRel = onePoleCoef (0.040, fs);
    heatLpCoef  = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * juce::jmin (14000.0, fs * 0.45) / (fs * 2.0));
    dcR         = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 8.0 / fs);
    fbHpCoef    = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 280.0 / fs);
    fbLpCoef    = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 4200.0 / fs);
    verbHpCoef  = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 220.0 / fs);

    //--------------------------------------------------------------------------
    // OJU 2.0
    heatLpCoef1x = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * juce::jmin (14000.0, fs * 0.45) / fs);
    {
        const double centres[sibBands] = { 4500.0, 6000.0, 7500.0, 9500.0 };
        for (int b = 0; b < sibBands; ++b)
            cSib[(size_t) b] = makeSvf (SvfType::bandpass, fs, juce::jmin (centres[b], fs * 0.45), 2.5);
        sibEnvCoef = onePoleCoef (0.004, fs);
    }
    for (auto* v : { &levelMix, &levelAmt, &echoOnMix, &verbOnMix, &riderMix })
        v->reset (fs, ramp);
    echoOnMix.setCurrentAndTargetValue (1.0f);   // v1 behaviour: delay and reverb on
    verbOnMix.setCurrentAndTargetValue (1.0f);
    levelEnvCoef   = onePoleCoef (0.030, fs);
    levelTrackCoef = onePoleCoef (3.0, fs);
    levelAttCoef   = onePoleCoef (0.020, fs);
    levelRelCoef   = onePoleCoef (0.400, fs);
    voiceEnvAtt    = onePoleCoef (0.005, fs);
    voiceEnvRel    = onePoleCoef (0.200, fs);
    duckAttCoef    = onePoleCoef (0.030, fs);
    echoRelCoef    = onePoleCoef (0.350, fs);
    verbRelCoef    = onePoleCoef (0.700, fs);
    riderEnvCoef   = onePoleCoef (0.020, fs);
    riderTargetCoef = onePoleCoef (10.0, fs);   // the song's level: a long memory
    riderWarmCoef   = onePoleCoef (0.5, fs);    // ...learned quickly in the first second
    riderMoveCoef  = onePoleCoef (0.300, fs);
    preDelay.setSize (maxChannels, (int) std::ceil (fs * 0.06) + 4);
    verbDuckBuf.assign ((size_t) maxBlock, 0.0f);
    activeLatency = latency;

    prepared = true;
    reset();
}

void VocalChain::reset()
{
    if (! prepared)
        return;

    for (int c = 0; c < maxChannels; ++c)
    {
        sLowCut1[(size_t) c].reset(); sLowCut2[(size_t) c].reset(); sBody[(size_t) c].reset();
        sMud[(size_t) c].reset(); sPresence[(size_t) c].reset(); sAir[(size_t) c].reset();
    }
    heatLp.fill (0.0f); dcX1.fill (0.0f); dcY1.fill (0.0f);
    fbHp.fill (0.0f); fbHpX1.fill (0.0f); fbLp.fill (0.0f); verbHp.fill (0.0f); verbHpX1.fill (0.0f);
    comp = {}; crush = {};
    tameEnvHi = tameEnvFull = tameRedDb = 0.0f;
    crossover.reset();
    oversampler->reset();
    delayBuffer.clear();
    dryRing.clear();
    rawRing.clear();
    reverb.reset();
    reverbActive = false;
    delayWrite = ringWrite = 0;
    controlCountdown = 0;
    lastDivision = -1;
    tameLastFreq = 0.0f;
    reverbSizeTarget = -1.0f;

    // OJU 2.0
    for (auto& st : sSib) st.reset();
    sibEnv.fill (0.0f);
    sibTrackedHz = 6500.0f;
    levelEnvDb = -60.0f; levelTrackDb = -24.0f; levelGain = 0.0f;
    voiceEnv = 0.0f; echoDuckDb = verbDuckDb = 0.0f;
    preDelay.clear(); preDelayWrite = 0;
    riderEnvDb = -80.0f; riderTargetDb = -200.0f; riderDb = 0.0f; riderMeanDb = 0.0f; riderWarm = 0;   // target: not learned yet
    lastVerbType = 0;
}

void VocalChain::setTargets (const ChainSettings& s) noexcept
{
    inGain.setTargetValue (dbToGain (s.inputGainDb));
    outGain.setTargetValue (dbToGain (s.outputGainDb));
    amountMix.setTargetValue (juce::jlimit (0.0f, 1.0f, s.amount));
    bypassMix.setTargetValue (s.bypass ? 1.0f : 0.0f);
    ceilingMix.setTargetValue (s.ceilingOn ? 1.0f : 0.0f);

    lowCutMix.setTargetValue (s.lowCutOn ? 1.0f : 0.0f);
    eqMix.setTargetValue (s.eqOn ? 1.0f : 0.0f);
    tameMix.setTargetValue (s.tameOn ? 1.0f : 0.0f);
    pressMix.setTargetValue (s.pressOn ? 1.0f : 0.0f);
    heatMix.setTargetValue (s.heatOn ? 1.0f : 0.0f);
    spaceMix.setTargetValue (s.spaceOn ? 1.0f : 0.0f);

    lowCutFreq.setTargetValue (s.lowCutFreq);
    mudFreq.setTargetValue (s.mudFreq);
    presenceFreq.setTargetValue (s.presenceFreq);
    tameFreq.setTargetValue (s.tameFreq);
    bodyGain.setTargetValue (s.bodyGain);
    mudGain.setTargetValue (s.mudGain);
    presenceGain.setTargetValue (s.presenceGain);
    airGain.setTargetValue (s.airGain);

    tameAmount.setTargetValue (juce::jlimit (0.0f, 1.0f, s.tameAmount));
    makeup.setTargetValue (s.makeupDb);
    parallel.setTargetValue (s.extreme ? juce::jlimit (0.0f, 1.0f, s.parallel) : 0.0f);

    if (! juce::exactlyEqual (s.threshold, thresholdDb) || ! juce::exactlyEqual (s.ratio, ratio)
        || ! juce::exactlyEqual (s.attackMs, attackMs) || ! juce::exactlyEqual (s.releaseMs, releaseMs))
    {
        thresholdDb = s.threshold;
        ratio = juce::jmax (1.0f, s.ratio);
        attackMs = s.attackMs;
        releaseMs = s.releaseMs;
        updateDynamicsCoeffs();
    }

    heatDrive.setTargetValue (s.heatDriveDb);
    heatWet.setTargetValue (juce::jlimit (0.0f, 1.0f, s.heatMix));

    echoLevel.setTargetValue (juce::jlimit (0.0f, 1.0f, s.delayMix) * 0.75f);
    feedback.setTargetValue (juce::jlimit (0.0f, 0.9f, s.feedback));
    verbLevel.setTargetValue (juce::jlimit (0.0f, 1.0f, s.reverbMix) * 0.55f);

    if (s.delayDivision != lastDivision || std::abs (s.bpm - lastBpm) > 0.01)
    {
        const auto target = (float) juce::jlimit (8.0, (double) delayLength - 4.0, delaySecondsFor (s.delayDivision, s.bpm) * fs);
        if (lastDivision < 0)
            delaySamples.setCurrentAndTargetValue (target);
        else
            delaySamples.setTargetValue (target);
        lastDivision = s.delayDivision;
        lastBpm = s.bpm;
    }

    if (std::abs (s.reverbSize - reverbSizeTarget) > 1.0e-4f || s.verbType != lastVerbType)
    {
        reverbSizeTarget = s.reverbSize;
        lastVerbType = s.verbType;
        const float size = juce::jlimit (0.0f, 1.0f, s.reverbSize);
        juce::Reverb::Parameters p;
        p.roomSize = 0.35f + 0.6f * size;
        p.damping = 0.55f - 0.25f * size;
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        p.freezeMode = 0.0f;
        double preDelayMs = 0.0;
        switch (s.verbType)     // OJU 2.0 reverb types (0 = the v1 "classic" voicing above)
        {
            case 1: p.roomSize = 0.40f + 0.30f * size; p.damping = 0.62f; p.width = 0.8f; preDelayMs = 8.0;  break;  // room
            case 2: p.roomSize = 0.70f + 0.22f * size; p.damping = 0.22f; p.width = 1.0f; preDelayMs = 20.0; break;  // plate
            case 3: p.roomSize = 0.84f + 0.14f * size; p.damping = 0.45f; p.width = 1.0f; preDelayMs = 35.0; break;  // hall
            default: break;
        }
        preDelaySamples = juce::jlimit (0, preDelay.getNumSamples() - 2, (int) std::round (preDelayMs * 0.001 * fs));
        reverb.setParameters (p);
    }

    //--------------------------------------------------------------------------
    // OJU 2.0
    trackMode = s.trackMode;
    activeLatency = trackMode ? 0 : latency;
    deessAuto = s.deessAuto && s.tameOn;
    if (deessAuto)
        tameFreq.setTargetValue (juce::jlimit (3500.0f, 10000.0f, sibTrackedHz * 0.82f));
    levelMix.setTargetValue (s.levelOn ? 1.0f : 0.0f);
    levelAmt.setTargetValue (juce::jlimit (0.0f, 1.0f, s.levelAmount));
    echoOnMix.setTargetValue (s.echoOn ? 1.0f : 0.0f);
    verbOnMix.setTargetValue (s.verbOn ? 1.0f : 0.0f);
    echoDuckAmt = juce::jlimit (0.0f, 1.0f, s.echoDuck);
    verbDuckAmt = juce::jlimit (0.0f, 1.0f, s.verbDuck);
    riderMix.setTargetValue (s.riderOn ? 1.0f : 0.0f);
    riderAmt = juce::jlimit (0.0f, 1.0f, s.riderAmount);
}

void VocalChain::updateDynamicsCoeffs() noexcept
{
    compAttCoef  = onePoleCoef (attackMs * 0.001, fs);
    compRelCoef  = onePoleCoef (releaseMs * 0.001, fs);
    crushAttCoef = onePoleCoef (0.001, fs);
    crushRelCoef = onePoleCoef (0.070, fs);
}

void VocalChain::updateShapeCoeffs() noexcept
{
    const int step = controlInterval;
    const double lc = lowCutFreq.skip (step);
    cLowCut1  = makeSvf (SvfType::highpass, fs, lc, shape::lowCutQ1);
    cLowCut2  = makeSvf (SvfType::highpass, fs, lc, shape::lowCutQ2);
    cBody     = makeSvf (SvfType::lowShelf, fs, shape::bodyFreq, shape::bodyQ, bodyGain.skip (step));
    cMud      = makeSvf (SvfType::bell, fs, mudFreq.skip (step), shape::mudQ, mudGain.skip (step));
    cPresence = makeSvf (SvfType::bell, fs, presenceFreq.skip (step), shape::presenceQ, presenceGain.skip (step));
    cAir      = makeSvf (SvfType::highShelf, fs, shape::airFreq, shape::airQ, airGain.skip (step));

    const float tf = tameFreq.skip (step);
    if (std::abs (tf - tameLastFreq) > 0.5f)
    {
        crossover.setCutoffFrequency (juce::jmin (tf, (float) (fs * 0.45)));
        tameLastFreq = tf;
    }
}

//==============================================================================
void VocalChain::process (juce::AudioBuffer<float>& buffer, const ChainSettings& s,
                          const float* const* rawSource, PostHeatInsert* insert) noexcept
{
    if (! prepared)
        return;

    setTargets (s);

    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
        processChunk (buffer, start, juce::jmin (maxBlock, total - start), s, rawSource, insert);
}

void VocalChain::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, const ChainSettings&,
                               const float* const* rawSource, PostHeatInsert* insert) noexcept
{
    const int nch = juce::jmin (buffer.getNumChannels(), channels);
    if (nch <= 0 || n <= 0)
        return;

    float* ch[maxChannels] = { buffer.getWritePointer (0, start), nch > 1 ? buffer.getWritePointer (1, start) : nullptr };

    float inPk = 0.0f, grMax = 0.0f, tameMax = 0.0f, levelMax = 0.0f;

    // ------------------------------------------------------------ Stage A
    for (int i = 0; i < n; ++i)
    {
        if (--controlCountdown <= 0)
        {
            updateShapeCoeffs();
            controlCountdown = controlInterval;
        }

        const float gIn = inGain.getNextValue();
        const float mLc = lowCutMix.getNextValue();
        const float mEq = eqMix.getNextValue();
        const float mTame = tameMix.getNextValue();
        const float mPress = pressMix.getNextValue();
        const float amt = tameAmount.getNextValue();
        const float mk = makeup.getNextValue();
        const float par = parallel.getNextValue();

        const int ringPos = (ringWrite + i) % ringSize;
        float x[maxChannels] {};

        for (int c = 0; c < nch; ++c)
        {
            const auto uc = (size_t) c;
            const float src = ch[c][i];
            const float raw = rawSource != nullptr ? rawSource[c][start + i] : src;   // OJU 2.0: pre-chain modules
            inPk = juce::jmax (inPk, std::abs (raw));
            rawRing.setSample (c, ringPos, raw);

            float v = src * gIn;
            dryRing.setSample (c, ringPos, raw * gIn);

            if (mLc > 0.0f)
            {
                const float y = sLowCut2[uc].process (cLowCut2, sLowCut1[uc].process (cLowCut1, v));
                v += mLc * (y - v);
            }
            if (mEq > 0.0f)
            {
                float y = sBody[uc].process (cBody, v);
                y = sMud[uc].process (cMud, y);
                y = sPresence[uc].process (cPresence, y);
                y = sAir[uc].process (cAir, y);
                v += mEq * (y - v);
            }
            x[c] = v;
        }

        // ---- Tame (split-band de-esser, linked detection)
        if (mTame > 0.0f)
        {
            float lo[maxChannels] {}, hi[maxChannels] {};
            float hiLevel = 0.0f, fullLevel = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                crossover.processSample (c, x[c], lo[c], hi[c]);
                hiLevel = juce::jmax (hiLevel, std::abs (hi[c]));
                fullLevel = juce::jmax (fullLevel, std::abs (x[c]));
            }

            int sibLoudest = -1;
            if (deessAuto)   // OJU 2.0: measure where the S's actually live
            {
                float mono = 0.0f;
                for (int c = 0; c < nch; ++c)
                    mono += x[c];
                sibLoudest = 0;
                for (int b = 0; b < sibBands; ++b)
                {
                    const float e = std::abs (sSib[(size_t) b].process (cSib[(size_t) b], mono));
                    auto& env = sibEnv[(size_t) b];
                    env = e + sibEnvCoef * (env - e);
                    if (env > sibEnv[(size_t) sibLoudest]) sibLoudest = b;
                }
            }

            tameEnvHi   = hiLevel   + (hiLevel   > tameEnvHi   ? tameEnvAtt : tameEnvRel) * (tameEnvHi - hiLevel);
            tameEnvFull = fullLevel + (fullLevel > tameEnvFull ? tameEnvAtt : tameEnvRel) * (tameEnvFull - fullLevel);

            float target = 0.0f;
            if (tameEnvHi > 0.0005f && amt > 0.0f)
            {
                const float ratioDb = gainToDb (tameEnvHi / (tameEnvFull + 1.0e-9f));
                const float thresh = -4.0f - 12.0f * amt;
                target = juce::jlimit (0.0f, 3.0f + 15.0f * amt, (ratioDb - thresh) * 1.2f) * juce::jmin (1.0f, amt * 5.0f);
            }
            // OJU 2.0 auto band: only learn during real S moments (high band dominating the voice)
            // (the S test uses the fixed 4-10 kHz bands, not the moving split, so it can't drift)
            const float sibSum = sibEnv[0] + sibEnv[1] + sibEnv[2] + sibEnv[3];
            if (sibLoudest >= 0 && sibSum > 0.02f && sibSum > 0.35f * tameEnvFull)
            {
                constexpr float centres[sibBands] = { 4500.0f, 6000.0f, 7500.0f, 9500.0f };
                const float weight = juce::jmin (1.0f, sibSum * 10.0f);   // louder S's teach faster
                sibTrackedHz += 0.002f * weight * (centres[sibLoudest] - sibTrackedHz);
            }

            tameRedDb = target + (target > tameRedDb ? tameGainAtt : tameGainRel) * (tameRedDb - target);
            tameMax = juce::jmax (tameMax, tameRedDb * mTame);

            const float gHi = dbToGain (-tameRedDb);
            for (int c = 0; c < nch; ++c)
            {
                const float y = lo[c] + hi[c] * gHi;
                x[c] += mTame * (y - x[c]);
            }
        }

        // ---- OJU 2.0 Compress stage 1: a slow leveler with an auto threshold
        const float mLevel = levelMix.getNextValue();
        const float lAmt = levelAmt.getNextValue();
        if (mLevel > 0.0f)
        {
            float pw = 0.0f;
            for (int c = 0; c < nch; ++c)
                pw = juce::jmax (pw, x[c] * x[c]);
            const float instDb = 4.3429448f * std::log (pw + 1.0e-12f);
            levelEnvDb = instDb + levelEnvCoef * (levelEnvDb - instDb);
            if (levelEnvDb > -50.0f)
                levelTrackDb = levelEnvDb + levelTrackCoef * (levelTrackDb - levelEnvDb);

            const float thresh = levelTrackDb + 2.0f - 6.0f * lAmt;
            const float lRatio = 1.5f + 2.5f * lAmt;
            const float target = levelEnvDb > thresh ? (levelEnvDb - thresh) * (1.0f - 1.0f / lRatio) : 0.0f;
            levelGain = target + (target > levelGain ? levelAttCoef : levelRelCoef) * (levelGain - target);
            levelMax = juce::jmax (levelMax, levelGain * mLevel);
            const float makeupLev = juce::jlimit (0.0f, 6.0f, 0.5f * (levelTrackDb - thresh) * (1.0f - 1.0f / lRatio));
            const float g = dbToGain (makeupLev - levelGain);
            for (int c = 0; c < nch; ++c)
                x[c] += mLevel * (x[c] * g - x[c]);
        }

        // ---- Press (soft-knee feed-forward, linked) + Extreme parallel stage
        if (mPress > 0.0f)
        {
            float level = 0.0f;
            for (int c = 0; c < nch; ++c)
                level = juce::jmax (level, std::abs (x[c]));

            const float xg = gainToDb (level);
            const float xl = computeReduction (xg, thresholdDb, ratio);
            comp.y1 = juce::jmax (xl, compRelCoef * comp.y1 + (1.0f - compRelCoef) * xl);
            comp.yl = compAttCoef * comp.yl + (1.0f - compAttCoef) * comp.y1;
            grMax = juce::jmax (grMax, comp.yl * mPress);
            const float g = dbToGain (mk - comp.yl);

            float gCrush = 0.0f;
            if (par > 0.0f)
            {
                const float xl2 = computeReduction (xg, thresholdDb - 10.0f, 10.0f);
                crush.y1 = juce::jmax (xl2, crushRelCoef * crush.y1 + (1.0f - crushRelCoef) * xl2);
                crush.yl = crushAttCoef * crush.yl + (1.0f - crushAttCoef) * crush.y1;
                gCrush = dbToGain (mk + 7.0f - crush.yl) * par * 0.6f;
            }
            const float norm = 1.0f / (1.0f + par * 0.42f);

            for (int c = 0; c < nch; ++c)
            {
                const float y = x[c] * (g + gCrush) * norm;
                x[c] += mPress * (y - x[c]);
            }
        }
        else
        {
            comp = {}; crush = {};
        }

        for (int c = 0; c < nch; ++c)
            ch[c][i] = x[c];
    }

    // ------------------------------------------------------------ Stage B: Heat (2x oversampled)
    if (trackMode)
    {
        // OJU 2.0 Track mode: the same Heat curve at 1x, so the plugin adds no latency.
        for (int i = 0; i < n; ++i)
        {
            const float on = heatMix.getNextValue();
            const float drive = dbToGain (heatDrive.getNextValue());
            const float wet = heatWet.getNextValue() * on;
            if (wet <= 0.0f)
                continue;

            const float bias = std::tanh (heatAsym);
            const float norm = 0.25f / (std::tanh (drive * 0.25f + heatAsym) - bias);
            for (int c = 0; c < nch; ++c)
            {
                const float dry = ch[c][i];
                float y = (std::tanh (drive * dry + heatAsym) - bias) * norm;
                auto& lp = heatLp[(size_t) c];
                lp = y + heatLpCoef1x * (lp - y);
                ch[c][i] = dry + wet * (lp - dry);
            }
        }
    }
    else
    {
        juce::dsp::AudioBlock<float> block (ch, (size_t) nch, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int m = (int) up.getNumSamples();

        for (int i = 0; i < m; ++i)
        {
            const float on = heatMix.getNextValue();
            const float drive = dbToGain (heatDrive.getNextValue());
            const float wet = heatWet.getNextValue() * on;
            if (wet <= 0.0f)
                continue;

            const float bias = std::tanh (heatAsym);
            const float norm = 0.25f / (std::tanh (drive * 0.25f + heatAsym) - bias);

            for (int c = 0; c < nch; ++c)
            {
                float* d = up.getChannelPointer ((size_t) c);
                const float dry = d[i];
                float y = (std::tanh (drive * dry + heatAsym) - bias) * norm;
                auto& lp = heatLp[(size_t) c];
                lp = y + heatLpCoef * (lp - y);
                d[i] = dry + wet * (lp - dry);
            }
        }

        oversampler->processSamplesDown (block);
    }

    {
        for (int c = 0; c < nch; ++c)
        {
            auto& x1 = dcX1[(size_t) c];
            auto& y1 = dcY1[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const float xin = ch[c][i];
                const float y = xin - x1 + dcR * y1;
                x1 = xin;
                y1 = y;
                ch[c][i] = y;
            }
        }
    }

    // ------------------------------------------------------------ OJU 2.0: Breath control, Double + Width
    if (insert != nullptr)
        insert->process (ch, nch, n);

    // ------------------------------------------------------------ Stage C: Space, Amount, Output
    const bool verbWanted = verbLevel.getTargetValue() > 0.0f && spaceMix.getTargetValue() > 0.0f && verbOnMix.getTargetValue() > 0.0f;
    const bool runVerb = verbWanted || verbLevel.isSmoothing() || spaceMix.isSmoothing() || verbOnMix.isSmoothing()
                         || (reverbActive && verbLevel.getCurrentValue() > 0.0f);
    const bool anyDuck = echoDuckAmt > 0.0f || verbDuckAmt > 0.0f || echoDuckDb > 0.001f || verbDuckDb > 0.001f;

    for (int i = 0; i < n; ++i)
    {
        const float mSpace = spaceMix.getNextValue();
        const float echoBase = echoLevel.getNextValue() * mSpace;
        const float fb = feedback.getNextValue();
        const float dly = delaySamples.getNextValue();

        // OJU 2.0: delay on/off, and ducking while the vocal sings (1.0 exactly when unused)
        const float mEcho = echoOnMix.getNextValue();
        float duckE = 1.0f;
        if (anyDuck)
        {
            float lvl = 0.0f;
            for (int c = 0; c < nch; ++c)
                lvl = juce::jmax (lvl, std::abs (ch[c][i]));
            voiceEnv = lvl + (lvl > voiceEnv ? voiceEnvAtt : voiceEnvRel) * (voiceEnv - lvl);
            const bool singing = voiceEnv > 0.0056f;   // about -45 dBFS
            const float eT = singing ? 18.0f * echoDuckAmt : 0.0f;
            const float vT = singing ? 15.0f * verbDuckAmt : 0.0f;
            echoDuckDb = eT + (eT > echoDuckDb ? duckAttCoef : echoRelCoef) * (echoDuckDb - eT);
            verbDuckDb = vT + (vT > verbDuckDb ? duckAttCoef : verbRelCoef) * (verbDuckDb - vT);
            duckE = dbToGain (-echoDuckDb);
            verbDuckBuf[(size_t) i] = verbDuckDb;
        }
        const float echo = echoBase * (mEcho * duckE);

        float readPos = (float) delayWrite - dly;
        if (readPos < 0.0f)
            readPos += (float) delayLength;
        const int r0 = (int) readPos;
        const int r1 = (r0 + 1) % delayLength;
        const float frac = readPos - (float) r0;

        for (int c = 0; c < nch; ++c)
        {
            const auto uc = (size_t) c;
            const float x = ch[c][i];
            const float* db = delayBuffer.getReadPointer (c);
            const float tap = db[r0] + frac * (db[r1] - db[r0]);

            // Filtered repeats: high-pass then low-pass inside the loop
            const float hp = fbHpCoef * (fbHp[uc] + tap - fbHpX1[uc]);
            fbHpX1[uc] = tap;
            fbHp[uc] = hp;
            fbLp[uc] = hp + fbLpCoef * (fbLp[uc] - hp);
            const float rep = fbLp[uc];

            delayBuffer.setSample (c, delayWrite, std::tanh (x * mSpace + fb * rep));

            if (runVerb)
            {
                const float vhp = verbHpCoef * (verbHp[uc] + x - verbHpX1[uc]);
                verbHpX1[uc] = x;
                verbHp[uc] = vhp;
                if (preDelaySamples > 0)   // OJU 2.0 reverb types use a short pre-delay
                {
                    preDelay.setSample (c, preDelayWrite, vhp * 0.5f);
                    int r = preDelayWrite - preDelaySamples;
                    if (r < 0) r += preDelay.getNumSamples();
                    reverbBuffer.setSample (c, i, preDelay.getSample (c, r));
                }
                else
                {
                    reverbBuffer.setSample (c, i, vhp * 0.5f);
                }
            }

            ch[c][i] = x + echo * rep;
        }
        delayWrite = (delayWrite + 1) % delayLength;
        if (preDelaySamples > 0)
            preDelayWrite = (preDelayWrite + 1) % preDelay.getNumSamples();
    }

    if (runVerb)
    {
        reverbActive = true;
        if (nch > 1)
            reverb.processStereo (reverbBuffer.getWritePointer (0), reverbBuffer.getWritePointer (1), n);
        else
            reverb.processMono (reverbBuffer.getWritePointer (0), n);
    }
    else if (reverbActive)
    {
        reverb.reset();
        reverbActive = false;
    }

    float outPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float mVerbOn = verbOnMix.getNextValue();
        const float duckV = anyDuck ? dbToGain (-verbDuckBuf[(size_t) i]) : 1.0f;
        const float verbGain = runVerb ? verbLevel.getNextValue() * spaceMix.getCurrentValue() * (mVerbOn * duckV)
                                       : (verbLevel.skip (1), 0.0f);
        const float amtMix = amountMix.getNextValue();
        const float gOut = outGain.getNextValue();
        const float mCeil = ceilingMix.getNextValue();
        const float mBypass = bypassMix.getNextValue();

        const int readRing = ((ringWrite + i - activeLatency) % ringSize + ringSize) % ringSize;

        float wetv[maxChannels] {};
        for (int c = 0; c < nch; ++c)
        {
            float wet = ch[c][i];
            if (runVerb)
                wet += verbGain * reverbBuffer.getSample (c, i);
            wetv[c] = wet;
        }

        // OJU 2.0 Output: vocal rider (slowly rides the level towards the song's own average)
        const float mRide = riderMix.getNextValue();
        if (mRide > 0.0f)
        {
            float pw = 0.0f;
            for (int c = 0; c < nch; ++c)
                pw = juce::jmax (pw, wetv[c] * wetv[c]);
            const float instDb = 4.3429448f * std::log (pw + 1.0e-12f);
            riderEnvDb = instDb + riderEnvCoef * (riderEnvDb - instDb);
            if (riderTargetDb < -150.0f && riderEnvDb > -60.0f)
                riderTargetDb = riderEnvDb;   // learn the singer's level from the first sound
            float want = 0.0f;                // gaps and silence: relax back to unity
            if (riderTargetDb > -150.0f && riderEnvDb > std::max (-70.0f, riderTargetDb - 25.0f))
            {
                const bool warming = riderWarm < (int) fs;
                riderWarm += warming ? 1 : 0;
                riderTargetDb = riderEnvDb + (warming ? riderWarmCoef : riderTargetCoef) * (riderTargetDb - riderEnvDb);
                const float range = 2.0f + 8.0f * riderAmt;
                want = riderTargetDb - riderEnvDb;
                if (want > 0.0f)
                {
                    // Lift quiet words gently (half the range), and leave word tails, breaths and
                    // room alone: anything 9-15 dB under the running level fades out of the lift.
                    const float tailFade = juce::jlimit (0.0f, 1.0f, (15.0f - want) / 6.0f);
                    want = juce::jmin (want, 0.5f * range) * tailFade;
                }
                want = juce::jlimit (-range, range, want);
                // Even out the performance without making the whole vocal louder or quieter
                riderMeanDb = want + riderTargetCoef * (riderMeanDb - want);
                want = juce::jlimit (-range, range, want - riderMeanDb);
            }
            riderDb = want + riderMoveCoef * (riderDb - want);
            const float gRide = 1.0f + mRide * (dbToGain (riderDb) - 1.0f);
            for (int c = 0; c < nch; ++c)
                wetv[c] *= gRide;
        }

        for (int c = 0; c < nch; ++c)
        {
            const float wet = wetv[c];

            const float dry = dryRing.getSample (c, readRing);
            float y = (dry + amtMix * (wet - dry)) * gOut;
            y += mCeil * (softCeiling (y) - y);

            const float raw = rawRing.getSample (c, readRing);
            y += mBypass * (raw - y);

            if (! std::isfinite (y))
                y = 0.0f;
            ch[c][i] = y;
            outPk = juce::jmax (outPk, std::abs (y));
        }
    }

    ringWrite = (ringWrite + n) % ringSize;

    for (int c = 0; c < nch; ++c)
    {
        auto uc = (size_t) c;
        sLowCut1[uc].sanitise(); sLowCut2[uc].sanitise(); sBody[uc].sanitise();
        sMud[uc].sanitise(); sPresence[uc].sanitise(); sAir[uc].sanitise();
    }

    storeMax (inputPeak, inPk);
    storeMax (outputPeak, outPk);
    storeMax (pressGrDb, grMax);
    storeMax (tameGrDb, tameMax);
    storeMax (levelGrDb, levelMax);
    if (deessAuto)
        tameBandHz.store (juce::jlimit (3500.0f, 10000.0f, sibTrackedHz * 0.82f), std::memory_order_relaxed);
    riderGainDb.store (riderDb, std::memory_order_relaxed);
}

} // namespace oju
