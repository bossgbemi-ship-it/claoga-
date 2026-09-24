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

    if (std::abs (s.reverbSize - reverbSizeTarget) > 1.0e-4f)
    {
        reverbSizeTarget = s.reverbSize;
        juce::Reverb::Parameters p;
        p.roomSize = 0.35f + 0.6f * juce::jlimit (0.0f, 1.0f, s.reverbSize);
        p.damping = 0.55f - 0.25f * juce::jlimit (0.0f, 1.0f, s.reverbSize);
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        p.freezeMode = 0.0f;
        reverb.setParameters (p);
    }
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
void VocalChain::process (juce::AudioBuffer<float>& buffer, const ChainSettings& s) noexcept
{
    if (! prepared)
        return;

    setTargets (s);

    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
        processChunk (buffer, start, juce::jmin (maxBlock, total - start), s);
}

void VocalChain::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, const ChainSettings&) noexcept
{
    const int nch = juce::jmin (buffer.getNumChannels(), channels);
    if (nch <= 0 || n <= 0)
        return;

    float* ch[maxChannels] = { buffer.getWritePointer (0, start), nch > 1 ? buffer.getWritePointer (1, start) : nullptr };

    float inPk = 0.0f, grMax = 0.0f, tameMax = 0.0f;

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
            const float raw = ch[c][i];
            inPk = juce::jmax (inPk, std::abs (raw));
            rawRing.setSample (c, ringPos, raw);

            float v = raw * gIn;
            dryRing.setSample (c, ringPos, v);

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

            tameEnvHi   = hiLevel   + (hiLevel   > tameEnvHi   ? tameEnvAtt : tameEnvRel) * (tameEnvHi - hiLevel);
            tameEnvFull = fullLevel + (fullLevel > tameEnvFull ? tameEnvAtt : tameEnvRel) * (tameEnvFull - fullLevel);

            float target = 0.0f;
            if (tameEnvHi > 0.0005f && amt > 0.0f)
            {
                const float ratioDb = gainToDb (tameEnvHi / (tameEnvFull + 1.0e-9f));
                const float thresh = -4.0f - 12.0f * amt;
                target = juce::jlimit (0.0f, 3.0f + 15.0f * amt, (ratioDb - thresh) * 1.2f) * juce::jmin (1.0f, amt * 5.0f);
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

    // ------------------------------------------------------------ Stage C: Space, Amount, Output
    const bool verbWanted = verbLevel.getTargetValue() > 0.0f && spaceMix.getTargetValue() > 0.0f;
    const bool runVerb = verbWanted || verbLevel.isSmoothing() || spaceMix.isSmoothing() || (reverbActive && verbLevel.getCurrentValue() > 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const float mSpace = spaceMix.getNextValue();
        const float echo = echoLevel.getNextValue() * mSpace;
        const float fb = feedback.getNextValue();
        const float dly = delaySamples.getNextValue();

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
                reverbBuffer.setSample (c, i, vhp * 0.5f);
            }

            ch[c][i] = x + echo * rep;
        }
        delayWrite = (delayWrite + 1) % delayLength;
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
        const float verbGain = runVerb ? verbLevel.getNextValue() * spaceMix.getCurrentValue() : (verbLevel.skip (1), 0.0f);
        const float amtMix = amountMix.getNextValue();
        const float gOut = outGain.getNextValue();
        const float mCeil = ceilingMix.getNextValue();
        const float mBypass = bypassMix.getNextValue();

        const int readRing = ((ringWrite + i - latency) % ringSize + ringSize) % ringSize;

        for (int c = 0; c < nch; ++c)
        {
            float wet = ch[c][i];
            if (runVerb)
                wet += verbGain * reverbBuffer.getSample (c, i);

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
}

} // namespace oju
