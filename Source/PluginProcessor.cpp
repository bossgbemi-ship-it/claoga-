#include "PluginProcessor.h"
#include "ui/PluginEditor.h"

namespace oju
{
namespace
{
    const juce::Identifier roomTag ("ROOM"), extraTag ("OJU_EXTRA"), readTag ("READ"), lineTag ("LINE"), featTag ("FEATURES"),
                           slotTag ("SLOT"), textAttr ("text"), ideaAttr ("idea"), activeAttr ("activeSlot"),
                           indexAttr ("index"), versionAttr ("version");
}

OjuProcessor::OjuProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OJU", createParameterLayout())
{
    auto bind = [this] (Raw& r, const char* id)
    {
        r.p = apvts.getRawParameterValue (id);
        jassert (r.p != nullptr);
    };

    bind (pMode, ids::mode);           bind (pStyle, ids::style);         bind (pBypass, ids::bypass);
    bind (pInput, ids::inputGain);     bind (pOutput, ids::outputGain);   bind (pAmount, ids::amount);
    bind (pCeiling, ids::ceilingOn);
    bind (pLowCutOn, ids::lowCutOn);   bind (pLowCut, ids::lowCutFreq);   bind (pEqOn, ids::eqOn);
    bind (pBody, ids::bodyGain);       bind (pMudF, ids::mudFreq);        bind (pMudG, ids::mudGain);
    bind (pPresF, ids::presenceFreq);  bind (pPresG, ids::presenceGain);  bind (pAir, ids::airGain);
    bind (pTameOn, ids::tameOn);       bind (pTameF, ids::tameFreq);      bind (pTameAmt, ids::tameAmount);
    bind (pPressOn, ids::pressOn);     bind (pThresh, ids::pressThreshold); bind (pRatio, ids::pressRatio);
    bind (pAttack, ids::pressAttack);  bind (pRelease, ids::pressRelease); bind (pMakeup, ids::pressMakeup);
    bind (pParallel, ids::pressParallel);
    bind (pHeatOn, ids::heatOn);       bind (pDrive, ids::heatDrive);     bind (pHeatMix, ids::heatMix);
    bind (pSpaceOn, ids::spaceOn);     bind (pDelayTime, ids::delayTime); bind (pFeedback, ids::delayFeedback);
    bind (pDelayMix, ids::delayMix);   bind (pVerbSize, ids::reverbSize); bind (pVerbMix, ids::reverbMix);

    // OJU 2.0
    bind (pRole, ids::role);               bind (pLatencyMode, ids::latencyMode); bind (pLinkGroup, ids::linkGroup);
    bind (pDenoiseOn, ids::denoiseOn);     bind (pDenoiseAmt, ids::denoiseAmount); bind (pRoomAmt, ids::roomAmount);
    bind (pPlosiveOn, ids::plosiveOn);     bind (pPlosiveAmt, ids::plosiveAmount);
    bind (pTuneOn, ids::tuneOn);           bind (pTuneKeySource, ids::tuneKeySource); bind (pTuneKey, ids::tuneKey);
    bind (pTuneScale, ids::tuneScale);     bind (pTuneSpeed, ids::tuneSpeed);     bind (pTuneHumanize, ids::tuneHumanize);
    bind (pTuneMix, ids::tuneMix);         bind (pDeessAuto, ids::deessAuto);
    bind (pLevelOn, ids::levelOn);         bind (pLevelAmt, ids::levelAmount);
    bind (pBreathOn, ids::breathOn);       bind (pBreathAmt, ids::breathAmount);
    bind (pDoubleOn, ids::doubleOn);       bind (pDoubleAmt, ids::doubleAmount);  bind (pWidth, ids::width);
    bind (pHookOnly, ids::hookOnly);
    bind (pEchoOn, ids::echoOn);           bind (pEchoDuck, ids::echoDuck);
    bind (pVerbOn, ids::verbOn);           bind (pVerbType, ids::verbType);       bind (pVerbDuck, ids::verbDuck);
    bind (pRiderOn, ids::riderOn);         bind (pRiderAmt, ids::riderAmount);
    bind (pLimiterOn, ids::limiterOn);     bind (pLimiterCeiling, ids::limiterCeiling);
    bind (pCarveOn, ids::carveOn);         bind (pCarveDepth, ids::carveDepth);

    setLatencySamples (VocalChain::expectedLatencySamples());
    startTimerHz (20);
}

OjuProcessor::~OjuProcessor()
{
    stopTimer();
}

//==============================================================================
bool OjuProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void OjuProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    const int channels = juce::jmax (1, juce::jmin (2, getTotalNumOutputChannels()));
    engine.prepare (sampleRate, samplesPerBlock, channels);
    listener.setSampleRate (sampleRate);
    setLatencySamples (engine.latencyFor (readEngineSettings()));
}

void OjuProcessor::releaseResources()
{
    engine.reset();
}

ChainSettings OjuProcessor::readSettings() const noexcept
{
    ChainSettings s;
    s.bypass = pBypass.get() > 0.5f;
    s.inputGainDb = pInput.get();
    s.outputGainDb = pOutput.get();
    s.amount = pAmount.get() * 0.01f;
    s.ceilingOn = pCeiling.get() > 0.5f;

    s.lowCutOn = pLowCutOn.get() > 0.5f;
    s.lowCutFreq = pLowCut.get();
    s.eqOn = pEqOn.get() > 0.5f;
    s.bodyGain = pBody.get();
    s.mudFreq = pMudF.get();
    s.mudGain = pMudG.get();
    s.presenceFreq = pPresF.get();
    s.presenceGain = pPresG.get();
    s.airGain = pAir.get();

    s.tameOn = pTameOn.get() > 0.5f;
    s.tameFreq = pTameF.get();
    s.tameAmount = pTameAmt.get() * 0.01f;

    s.pressOn = pPressOn.get() > 0.5f;
    s.threshold = pThresh.get();
    s.ratio = pRatio.get();
    s.attackMs = pAttack.get();
    s.releaseMs = pRelease.get();
    s.makeupDb = pMakeup.get();
    s.parallel = pParallel.get() * 0.01f;
    s.extreme = pMode.get() > 0.5f;

    s.heatOn = pHeatOn.get() > 0.5f;
    s.heatDriveDb = pDrive.get();
    s.heatMix = pHeatMix.get() * 0.01f;

    s.spaceOn = pSpaceOn.get() > 0.5f;
    s.delayDivision = juce::jlimit (0, 4, (int) std::lround (pDelayTime.get()));
    s.feedback = pFeedback.get() * 0.01f;
    s.delayMix = pDelayMix.get() * 0.01f;
    s.reverbSize = pVerbSize.get() * 0.01f;
    s.reverbMix = pVerbMix.get() * 0.01f;
    s.bpm = hostBpm.load (std::memory_order_relaxed);

    // OJU 2.0 insertions into the v1 chain (defaults = v1)
    if (! v1Compare.load (std::memory_order_relaxed))
    {
        s.trackMode = pLatencyMode.get() > 0.5f;
        s.deessAuto = pDeessAuto.get() > 0.5f;
        s.levelOn = pLevelOn.get() > 0.5f;
        s.levelAmount = pLevelAmt.get() * 0.01f;
        s.echoOn = pEchoOn.get() > 0.5f;
        s.verbOn = pVerbOn.get() > 0.5f;
        s.echoDuck = pEchoDuck.get() * 0.01f;
        s.verbDuck = pVerbDuck.get() * 0.01f;
        s.verbType = juce::jlimit (0, 3, (int) std::lround (pVerbType.get()));
        s.riderOn = pRiderOn.get() > 0.5f;
        s.riderAmount = pRiderAmt.get() * 0.01f;
    }
    return s;
}

EngineSettings OjuProcessor::readEngineSettings() const noexcept
{
    EngineSettings e;
    e.chain = readSettings();
    if (v1Compare.load (std::memory_order_relaxed))
        return e;

    e.denoiseOn = pDenoiseOn.get() > 0.5f;
    e.denoiseAmount = pDenoiseAmt.get() * 0.01f;
    e.roomAmount = pRoomAmt.get() * 0.01f;
    e.plosiveOn = pPlosiveOn.get() > 0.5f;
    e.plosiveAmount = pPlosiveAmt.get() * 0.01f;
    e.limiterOn = pLimiterOn.get() > 0.5f;
    e.limiterCeilingDb = pLimiterCeiling.get();
    e.breathOn = pBreathOn.get() > 0.5f;
    e.breathAmount = pBreathAmt.get() * 0.01f;
    e.doubleOn = pDoubleOn.get() > 0.5f;
    e.doubleAmount = pDoubleAmt.get() * 0.01f;
    e.width = pWidth.get() * 0.01f;
    e.hookOnly = pHookOnly.get() > 0.5f;
    return e;
}

void OjuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, n);
    if (n == 0)
        return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 20.0 && *bpm < 400.0)
                    hostBpm.store (*bpm, std::memory_order_relaxed);

    const int chans = juce::jmin (2, numIn, buffer.getNumChannels());
    if (listener.isCapturing())
        listener.pushAudio (buffer.getArrayOfReadPointers(), chans, n);

    const auto t0 = juce::Time::getHighResolutionTicks();
    engine.process (buffer, readEngineSettings());

    // CPU meter: time spent in this callback relative to the audio it covers.
    const double spent = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double budget = n / juce::jmax (1.0, currentRate.load (std::memory_order_relaxed));
    const float load = (float) (spent / budget);
    const float prev = cpuLoad.load (std::memory_order_relaxed);
    cpuLoad.store (prev + 0.05f * (load - prev), std::memory_order_relaxed);
}

juce::AudioProcessorParameter* OjuProcessor::getBypassParameter() const
{
    return apvts.getParameter (ids::bypass);
}

juce::AudioProcessorEditor* OjuProcessor::createEditor()
{
    return new OjuEditor (*this);
}

//==============================================================================
void OjuProcessor::toggleListening()
{
    const auto s = listener.getState();
    if (s == Listener::State::listening || s == Listener::State::analysing)
        listener.cancel();
    else
        listener.begin ((int) std::lround (pMode.get()));
}

void OjuProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float norm = p->convertTo0to1 (realValue);
        if (std::abs (norm - p->getValue()) < 1.0e-6f)
            return;
        // Proper host gesture so the change is recorded, automatable and undoable.
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void OjuProcessor::applyBrain (const Features& f, bool rewriteRead)
{
    if (! f.valid)
        return;

    const auto result = decide (f, (int) std::lround (pStyle.get()), (int) std::lround (pMode.get()), hostBpm.load());
    for (const auto& [id, value] : result.values)
        setParam (id, value);

    if (rewriteRead)
    {
        readLines = result.read;
        idea = result.idea;
        ++readVersion;
    }
    refreshExtraCache();
}

void OjuProcessor::learnRoom()
{
    setParam (ids::denoiseOn, 1.0f);
    engine.getDenoiser().startLearning();
}

void OjuProcessor::timerCallback()
{
    applyPendingExtra();

    // OJU 2.0: a freshly learned room fingerprint gets saved with the session
    if (engine.getDenoiser().takeLearnedProfile (roomProfile))
    {
        roomProfileValid = true;
        refreshExtraCache();
    }

    // OJU 2.0: latency depends on which modules are on and on Track / Mix mode.
    if (const int lat = engine.latencyFor (readEngineSettings()); lat != getLatencySamples())
        setLatencySamples (lat);

    // Settle style/mode after a state load so it doesn't count as a user change.
    if (const int ls = loadedStyle.exchange (-1); ls >= 0) lastStyle = ls;
    if (const int lm = loadedMode.exchange (-1); lm >= 0)  lastMode = lm;

    Features f;
    if (listener.takeResult (f))
    {
        features = f;
        lastStyle = (int) std::lround (pStyle.get());
        lastMode = (int) std::lround (pMode.get());
        applyBrain (features, true);
        return;
    }

    const int style = (int) std::lround (pStyle.get());
    const int mode = (int) std::lround (pMode.get());
    if (lastStyle < 0) lastStyle = style;
    if (lastMode < 0)  lastMode = mode;

    if (style != lastStyle || mode != lastMode)
    {
        lastStyle = style;
        lastMode = mode;
        // Re-target from the last read without listening again. With no read yet,
        // the genre works as a preset: neutral measurements, genre targets.
        if (listener.getState() != Listener::State::listening)
        {
            if (features.valid)
                applyBrain (features, true);
            else
                applyBrain (neutralFeatures(), false);
        }
    }
}

Features OjuProcessor::neutralFeatures()
{
    Features f;
    f.valid = true;
    f.rmsP10 = -30.0f; f.rmsP50 = -18.0f; f.rmsP90 = -13.0f; f.rmsP95 = -11.0f;
    f.peakDb = -4.0f; f.crestDb = 14.0f; f.dynamicsDb = 14.0f;
    f.mudExcessDb = 1.5f; f.boxExcessDb = 0.5f; f.presenceDb = 0.0f; f.harshExcessDb = 0.0f;
    f.sibilanceDb = 2.0f; f.airDb = 0.0f; f.lowestVoiceHz = 140.0f;
    return f;
}

//==============================================================================
juce::File OjuProcessor::presetsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Made by Joseph").getChildFile ("OJU").getChildFile ("Presets");
}

bool OjuProcessor::savePreset (const juce::File& file)
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize()))
    {
        file.getParentDirectory().createDirectory();
        return xml->writeTo (file.withFileExtension ("ojupreset"));
    }
    return false;
}

bool OjuProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;
    juce::MemoryBlock mb;
    copyXmlToBinary (*xml, mb);
    // Presets are user actions: record them as host changes (undo / automation).
    setStateInformation (mb.getData(), (int) mb.getSize());
    applyPendingExtra();
    return true;
}

//==============================================================================
juce::ValueTree OjuProcessor::captureSoundState() const
{
    juce::ValueTree t (slotTag);
    for (const auto& id : soundParameterIds())
        if (auto* p = apvts.getParameter (id))
            t.setProperty (id, p->convertFrom0to1 (p->getValue()), nullptr);
    return t;
}

void OjuProcessor::restoreSoundState (const juce::ValueTree& tree)
{
    for (const auto& id : soundParameterIds())
        if (tree.hasProperty (id))
            setParam (id, (float) tree.getProperty (id));
}

void OjuProcessor::toggleAB()
{
    slots[activeSlot] = captureSoundState();
    activeSlot ^= 1;
    if (slots[activeSlot].isValid())
        restoreSoundState (slots[activeSlot]);
    else
        slots[activeSlot] = slots[activeSlot ^ 1].createCopy();
    refreshExtraCache();
}

juce::ValueTree OjuProcessor::featuresToTree (const Features& f)
{
    juce::ValueTree t (featTag);
    t.setProperty ("valid", f.valid, nullptr);
    t.setProperty ("mode", f.mode, nullptr);
    t.setProperty ("sampleRate", f.sampleRate, nullptr);
    t.setProperty ("voicedSeconds", f.voicedSeconds, nullptr);
    t.setProperty ("rmsP10", f.rmsP10, nullptr);
    t.setProperty ("rmsP50", f.rmsP50, nullptr);
    t.setProperty ("rmsP90", f.rmsP90, nullptr);
    t.setProperty ("rmsP95", f.rmsP95, nullptr);
    t.setProperty ("peakDb", f.peakDb, nullptr);
    t.setProperty ("crestDb", f.crestDb, nullptr);
    t.setProperty ("dynamicsDb", f.dynamicsDb, nullptr);
    t.setProperty ("rumbleDb", f.rumbleDb, nullptr);
    t.setProperty ("lowestVoiceHz", f.lowestVoiceHz, nullptr);
    t.setProperty ("mudFreq", f.mudFreq, nullptr);
    t.setProperty ("mudExcessDb", f.mudExcessDb, nullptr);
    t.setProperty ("boxFreq", f.boxFreq, nullptr);
    t.setProperty ("boxExcessDb", f.boxExcessDb, nullptr);
    t.setProperty ("presenceDb", f.presenceDb, nullptr);
    t.setProperty ("harshFreq", f.harshFreq, nullptr);
    t.setProperty ("harshExcessDb", f.harshExcessDb, nullptr);
    t.setProperty ("sibilanceFreq", f.sibilanceFreq, nullptr);
    t.setProperty ("sibilanceDb", f.sibilanceDb, nullptr);
    t.setProperty ("airDb", f.airDb, nullptr);

    juce::StringArray spec;
    for (auto v : f.spectrumDb)
        spec.add (juce::String (v, 2));
    t.setProperty ("spectrum", spec.joinIntoString (" "), nullptr);
    return t;
}

Features OjuProcessor::treeToFeatures (const juce::ValueTree& t)
{
    Features f;
    if (! t.isValid())
        return f;
    auto get = [&t] (const char* name, float def) { return (float) t.getProperty (name, def); };
    f.valid = (bool) t.getProperty ("valid", false);
    f.mode = (int) t.getProperty ("mode", 0);
    f.sampleRate = get ("sampleRate", f.sampleRate);
    f.voicedSeconds = get ("voicedSeconds", f.voicedSeconds);
    f.rmsP10 = get ("rmsP10", f.rmsP10);
    f.rmsP50 = get ("rmsP50", f.rmsP50);
    f.rmsP90 = get ("rmsP90", f.rmsP90);
    f.rmsP95 = get ("rmsP95", f.rmsP95);
    f.peakDb = get ("peakDb", f.peakDb);
    f.crestDb = get ("crestDb", f.crestDb);
    f.dynamicsDb = get ("dynamicsDb", f.dynamicsDb);
    f.rumbleDb = get ("rumbleDb", f.rumbleDb);
    f.lowestVoiceHz = get ("lowestVoiceHz", f.lowestVoiceHz);
    f.mudFreq = get ("mudFreq", f.mudFreq);
    f.mudExcessDb = get ("mudExcessDb", f.mudExcessDb);
    f.boxFreq = get ("boxFreq", f.boxFreq);
    f.boxExcessDb = get ("boxExcessDb", f.boxExcessDb);
    f.presenceDb = get ("presenceDb", f.presenceDb);
    f.harshFreq = get ("harshFreq", f.harshFreq);
    f.harshExcessDb = get ("harshExcessDb", f.harshExcessDb);
    f.sibilanceFreq = get ("sibilanceFreq", f.sibilanceFreq);
    f.sibilanceDb = get ("sibilanceDb", f.sibilanceDb);
    f.airDb = get ("airDb", f.airDb);

    juce::StringArray spec;
    spec.addTokens (t.getProperty ("spectrum").toString(), " ", {});
    for (int i = 0; i < juce::jmin (spec.size(), Features::spectrumPoints); ++i)
        f.spectrumDb[(size_t) i] = spec[i].getFloatValue();
    return f;
}

void OjuProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto old = state.getChildWithName (extraTag); old.isValid())
        state.removeChild (old, nullptr);

    juce::ValueTree extra;
    {
        // Built on the message thread (refreshExtraCache), so saving from any thread is safe.
        const juce::ScopedLock sl (extraCacheLock);
        extra = extraCache.createCopy();
    }
    if (! extra.isValid())
        extra = juce::ValueTree (extraTag);

    state.appendChild (extra, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OjuProcessor::refreshExtraCache()
{
    juce::ValueTree extra (extraTag);
    extra.setProperty (versionAttr, 1, nullptr);

    juce::ValueTree read (readTag);
    read.setProperty (ideaAttr, idea, nullptr);
    for (const auto& l : readLines)
    {
        juce::ValueTree line (lineTag);
        line.setProperty (textAttr, l, nullptr);
        read.appendChild (line, nullptr);
    }
    extra.appendChild (read, nullptr);
    extra.appendChild (featuresToTree (features), nullptr);

    if (roomProfileValid)
    {
        juce::StringArray values;
        for (auto v : roomProfile)
            values.add (juce::String (v, 2));
        juce::ValueTree room (roomTag);
        room.setProperty ("db", values.joinIntoString (" "), nullptr);
        extra.appendChild (room, nullptr);
    }

    extra.setProperty (activeAttr, activeSlot, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        if (slots[i].isValid())
        {
            auto slot = slots[i].createCopy();
            slot.setProperty (indexAttr, i, nullptr);
            extra.appendChild (slot, nullptr);
        }
    }

    const juce::ScopedLock sl (extraCacheLock);
    extraCache = extra;
}

void OjuProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    auto extra = tree.getChildWithName (extraTag);
    if (extra.isValid())
        tree.removeChild (extra, nullptr);

    // Parameters missing from the preset (e.g. OJU 2.0 modules in a v1 preset) go back to
    // their defaults - all new modules off - instead of keeping whatever was set before.
    juce::StringArray present;
    for (auto child : tree)
        present.add (child.getProperty ("id").toString());

    apvts.replaceState (tree);
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (! present.contains (ranged->paramID))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    loadedStyle.store ((int) std::lround (pStyle.get()));
    loadedMode.store ((int) std::lround (pMode.get()));

    if (! extra.isValid())
        return;

    {
        const juce::ScopedLock sl (pendingLock);
        pendingExtra = extra;
    }

    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        applyPendingExtra();
}

void OjuProcessor::applyPendingExtra()
{
    juce::ValueTree extra;
    {
        const juce::ScopedLock sl (pendingLock);
        std::swap (extra, pendingExtra);
    }
    if (! extra.isValid())
        return;

    auto read = extra.getChildWithName (readTag);
    readLines.clear();
    idea = read.getProperty (ideaAttr).toString();
    for (auto line : read)
        readLines.add (line.getProperty (textAttr).toString());
    features = treeToFeatures (extra.getChildWithName (featTag));

    // Learn Room fingerprint (OJU 2.0); v1 presets have none
    {
        auto room = extra.getChildWithName (roomTag);
        juce::StringArray values;
        values.addTokens (room.getProperty ("db").toString(), " ", {});
        roomProfileValid = room.isValid() && values.size() == Denoiser::profileBands;
        if (roomProfileValid)
            for (int i = 0; i < Denoiser::profileBands; ++i)
                roomProfile[(size_t) i] = values[i].getFloatValue();
        engine.getDenoiser().setProfile (roomProfile, roomProfileValid);
    }

    activeSlot = juce::jlimit (0, 1, (int) extra.getProperty (activeAttr, 0));
    slots[0] = {};
    slots[1] = {};
    for (auto child : extra)
        if (child.hasType (slotTag))
            slots[juce::jlimit (0, 1, (int) child.getProperty (indexAttr, 0))] = child.createCopy();
    ++readVersion;
    refreshExtraCache();
}

} // namespace oju

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new oju::OjuProcessor();
}
