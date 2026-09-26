#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/Engine.h"
#include "brain/Listener.h"
#include "brain/Brain.h"

namespace oju
{
class OjuProcessor : public juce::AudioProcessor,
                     private juce::Timer
{
public:
    OjuProcessor();
    ~OjuProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OJU"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    // Brain / UI API (message thread)
    void toggleListening();
    Listener::State getListenState() const noexcept { return listener.getState(); }
    float getListenProgress() const noexcept        { return listener.getProgress(); }

    const juce::StringArray& getReadLines() const noexcept { return readLines; }
    const juce::String& getIdea() const noexcept           { return idea; }
    const Features& getFeatures() const noexcept           { return features; }
    int getReadVersion() const noexcept                    { return readVersion; }

    // A/B compare
    void toggleAB();
    int getActiveSlot() const noexcept { return activeSlot; }

    // Snapshot of the current (target) settings, for the EQ curve display.
    ChainSettings readSettings() const noexcept;
    EngineSettings readEngineSettings() const noexcept;

    // OJU 2.0: hear the plain OJU v1 sound (all 2.0 modules off) for comparison.
    void setV1Compare (bool on) noexcept { v1Compare.store (on); }
    bool isV1Compare() const noexcept    { return v1Compare.load(); }
    double getCurrentSampleRate() const noexcept { return currentRate.load(); }
    double getHostBpm() const noexcept           { return hostBpm.load(); }

    VocalChain& getChain() noexcept { return engine.getChain(); }
    Engine& getEngine() noexcept    { return engine; }
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

    // OJU 2.0
    void learnRoom();
    bool isLearningRoom() noexcept          { return engine.getDenoiser().isLearning(); }
    float getRoomLearnProgress() noexcept   { return engine.getDenoiser().learnProgress(); }
    bool hasRoomProfile() const noexcept    { return roomProfileValid; }

    float getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }
    static juce::File presetsDirectory();
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    void setParameterFromUI (const juce::String& id, float realValue) { setParam (id, realValue); }
    float getParameterValue (const juce::String& id) const
    {
        if (auto* p = apvts.getParameter (id)) return p->convertFrom0to1 (p->getValue());
        return 0.0f;
    }

    // For tests: process pending analysis/style changes immediately.
    void pumpMessageThreadWork() { timerCallback(); }

private:
    void timerCallback() override;
    void applyBrain (const Features& f, bool rewriteRead);
    void setParam (const juce::String& id, float realValue);
    juce::ValueTree captureSoundState() const;
    void restoreSoundState (const juce::ValueTree& tree);
    void applyPendingExtra();
    void refreshExtraCache();
    static juce::ValueTree featuresToTree (const Features& f);
    static Features neutralFeatures();
    static Features treeToFeatures (const juce::ValueTree& t);

    juce::AudioProcessorValueTreeState apvts;
    Engine engine;
    Listener listener;
    juce::AudioBuffer<float> scratch;

    struct Raw
    {
        std::atomic<float>* p = nullptr;
        float get() const noexcept { return p->load (std::memory_order_relaxed); }
    };
    Raw pMode, pStyle, pBypass, pInput, pOutput, pAmount, pCeiling,
        pLowCutOn, pLowCut, pEqOn, pBody, pMudF, pMudG, pPresF, pPresG, pAir,
        pTameOn, pTameF, pTameAmt,
        pPressOn, pThresh, pRatio, pAttack, pRelease, pMakeup, pParallel,
        pHeatOn, pDrive, pHeatMix,
        pSpaceOn, pDelayTime, pFeedback, pDelayMix, pVerbSize, pVerbMix,
        // OJU 2.0
        pRole, pLatencyMode, pDenoiseOn, pDenoiseAmt, pRoomAmt, pPlosiveOn, pPlosiveAmt,
        pTuneOn, pTuneKeySource, pTuneKey, pTuneScale, pTuneSpeed, pTuneHumanize, pTuneMix,
        pDeessAuto, pLevelOn, pLevelAmt, pBreathOn, pBreathAmt, pDoubleOn, pDoubleAmt, pWidth, pHookOnly,
        pEchoOn, pEchoDuck, pVerbOn, pVerbType, pVerbDuck, pRiderOn, pRiderAmt, pLimiterOn, pLimiterCeiling,
        pCarveOn, pCarveDepth, pLinkGroup;

    std::atomic<bool> v1Compare { false };
    std::atomic<float> cpuLoad { 0.0f };

    std::atomic<double> hostBpm { 120.0 }, currentRate { 48000.0 };

    // Message-thread state
    Features features;
    juce::StringArray readLines;
    juce::String idea;
    int readVersion = 0;
    int lastStyle = -1, lastMode = -1;
    std::atomic<int> loadedStyle { -1 }, loadedMode { -1 };

    std::array<float, Denoiser::profileBands> roomProfile {};
    bool roomProfileValid = false;

    juce::CriticalSection pendingLock, extraCacheLock;
    juce::ValueTree extraCache;
    juce::ValueTree pendingExtra;

    juce::ValueTree slots[2];
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OjuProcessor)
};

} // namespace oju
