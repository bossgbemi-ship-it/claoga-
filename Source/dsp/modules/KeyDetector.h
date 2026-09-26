#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace oju
{
/*  Hears the key of a beat. The audio thread pushes the mono beat into a lock-free
    FIFO; a background thread turns it into a chroma profile (the energy of each of the
    12 pitch classes, bass included), keeps a ~30 s memory, and matches it against the
    major/minor key profiles.                                                          */
class KeyDetector : private juce::Thread
{
public:
    KeyDetector();
    ~KeyDetector() override;

    void setSampleRate (double sr) noexcept { sampleRate.store (sr); }
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;   // audio thread
    void restart() noexcept { restartRequested.store (true); notify(); }

    int   getKey() const noexcept         { return key.load(); }
    bool  isMinor() const noexcept        { return minor.load(); }
    float getConfidence() const noexcept  { return confidence.load(); }
    float getHeardSeconds() const noexcept { return heard.load(); }

    // Offline: the key of an audio file - its tag first, else by listening (up to 90 s).
    struct FileKey { int key = -1; bool minor = true; float confidence = 0.0f; juce::String how; };
    static FileKey keyFromFile (const juce::File& file);
    static bool parseKeyText (const juce::String& text, int& key, bool& minor, bool& modeKnown);

    // Chroma of one magnitude spectrum, added into 'chroma'.
    static void addChroma (const float* magnitudes, int bins, double binHz, std::array<double, 12>& chroma);

private:
    void run() override;

    static constexpr int fifoSize = 1 << 17;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> restartRequested { false };
    std::atomic<int> key { -1 };
    std::atomic<bool> minor { true };
    std::atomic<float> confidence { 0.0f }, heard { 0.0f };
};

} // namespace oju
