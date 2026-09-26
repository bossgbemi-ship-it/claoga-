#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

struct DenoiseState;

namespace oju
{
/*  Denoise: RNNoise tells us, every 10 ms, how much of each of 22 frequency bands is
    noise. We apply those gains with our own STFT at the session's sample rate (so the
    vocal itself is never resampled), with:
      - a floor (Natural: gentle, Extreme: aggressive) so it never gates or chops,
      - slow per-bin release so sustained notes and ad-lib tails ring out naturally,
      - an optional "room fingerprint" (Learn Room) for steady background noise.
    Latency: one FFT frame (about 21 ms at 44.1/48 kHz).                                  */
class Denoiser
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int profileBands = 64;           // room fingerprint, log-spaced 40 Hz - 20 kHz

    Denoiser();
    ~Denoiser();

    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latencySamples() const noexcept { return fftSize; }

    // amount 0..1, room 0..1 (how much of the learned room to remove), extreme = stronger
    void process (float* const* ch, int nch, int n, float amount, float room, bool extreme) noexcept;

    // Learn Room: fingerprints ~2 s of silence (frames with singing are skipped)
    void startLearning() noexcept         { learnRequest.store (true); }
    bool isLearning() const noexcept      { return learning.load(); }
    float learnProgress() const noexcept  { return learnProgressValue.load(); }

    // Room profile exchange with the message thread (for saving in the preset)
    bool takeLearnedProfile (std::array<float, profileBands>& out);           // message thread
    void setProfile (const std::array<float, profileBands>& dbValues, bool valid); // message thread
    bool hasRoomProfile() const noexcept  { return roomValid.load(); }

    std::atomic<float> vad { 0.0f }, reductionDb { 0.0f };

private:
    void processFrame (int nch, float amount, float room, bool extreme) noexcept;
    void runRnnoise (const float* mono, int n) noexcept;
    void applyPendingProfile() noexcept;
    float profileFreq (int band) const noexcept;

    double fs = 48000.0;
    int fftOrder = 10, fftSize = 1024, hop = 256, bins = 513;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;

    // streaming STFT state
    std::array<std::vector<float>, maxChannels> inRing, outAccum;
    std::vector<float> frame, monoScratch;
    int inPos = 0, hopCount = 0, outPos = 0;

    // gains
    std::vector<float> binGain, binPower, noiseProfile, binHz;
    std::array<float, 32> bandGains {};
    float releasePerFrame = 0.9f;

    // RNNoise analysis at 48 kHz (linear-interpolated side chain)
    DenoiseState* rnn = nullptr;
    std::array<float, 480> rnnFrame {};
    int rnnFill = 0;
    double resamplePos = 0.0, resampleStep = 1.0;
    float lastInput = 0.0f;
    float vadValue = 0.0f;

    // Learn Room
    std::atomic<bool> learnRequest { false }, learning { false }, roomValid { false };
    std::atomic<float> learnProgressValue { 0.0f };
    std::vector<double> learnAccum;
    int learnFrames = 0, learnTarget = 0;
    std::array<float, profileBands> learnedOut {};
    std::atomic<bool> learnedReady { false };
    std::array<float, profileBands> pendingProfile {};
    std::atomic<int> pendingState { 0 };               // 0 none, 1 set valid, 2 clear
};

} // namespace oju
