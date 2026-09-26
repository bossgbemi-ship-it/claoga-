#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "Features.h"
#include "../dsp/modules/PitchDetector.h"

namespace oju
{
/*  The ears of OJU.

    The audio thread pushes the (mono-summed) input into a lock-free single
    producer / single consumer FIFO while a capture is running. A background
    thread drains it, runs a voice-activity gate so only moments where the
    artist is actually singing count towards the listening time, and when it
    has heard enough runs the FFT analysis and publishes a Features result.  */
class Listener : private juce::Thread
{
public:
    enum class State { idle = 0, listening, analysing, done, failed };

    Listener();
    ~Listener() override;

    void setSampleRate (double sr) noexcept { sampleRate.store (sr); }

    // Audio thread. Never blocks, never allocates.
    void pushAudio (const float* const* channels, int numChannels, int numSamples) noexcept;

    // Message thread
    void begin (int mode);
    void cancel();
    bool takeResult (Features& out);

    State getState() const noexcept     { return (State) state.load(); }
    float getProgress() const noexcept  { return progress.load(); }
    bool  isCapturing() const noexcept  { return capturing.load (std::memory_order_relaxed); }

    // Pure analysis entry point (also used by the tests).
    static Features analyse (const std::vector<float>& audio, const std::vector<char>& voicedFrames,
                             int frameLength, double sampleRate, int mode);

    // OJU 2.0: key from a 12-bin pitch-class histogram (Krumhansl-Kessler profiles).
    // Also used by OJU Beat (chroma). Returns root (0..11) and sets minor/confidence.
    static int estimateKey (const std::array<double, 12>& histogram, bool& minor, float& confidence);

    static constexpr double naturalSeconds = 10.0, extremeSeconds = 10.0;   // OJU 2.0: Auto listens for 10 s

private:
    void run() override;
    void startCapture();
    void drainInto (bool keep);
    void updateVoiceGate();

    static constexpr int fifoSize = 1 << 18;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int>   state { (int) State::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool>  capturing { false }, startRequested { false }, cancelRequested { false };
    std::atomic<int>   requestedMode { 0 };

    // Analysis-thread-only state
    std::vector<float> capture;
    std::vector<char> voiced;
    std::vector<float> frameDb;
    double captureRate = 48000.0;
    int frameLength = 960, framesDone = 0, voicedFrames = 0, captureMode = 0;
    float noiseFloorDb = -60.0f;

    juce::CriticalSection resultLock;
    Features result;
    bool resultReady = false;
};

} // namespace oju
