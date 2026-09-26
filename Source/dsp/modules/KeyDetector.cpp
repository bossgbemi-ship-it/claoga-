#include "KeyDetector.h"
#include "../../brain/Listener.h"

namespace oju
{
namespace
{
    constexpr int fftOrder = 13;
    constexpr int fftSize = 1 << fftOrder;

    // Where a hop's worth of samples lands in a spectrum + chroma
    struct ChromaEngine
    {
        juce::dsp::FFT fft { fftOrder };
        std::vector<float> window, buffer;
        ChromaEngine()
        {
            window.resize ((size_t) fftSize);
            buffer.resize ((size_t) fftSize * 2);
            for (int i = 0; i < fftSize; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
        }
        void frame (const float* samples, double sr, std::array<double, 12>& chroma)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                buffer[(size_t) i] = samples[i] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
            KeyDetector::addChroma (buffer.data(), fftSize / 2 + 1, sr / fftSize, chroma);
        }
    };
}

void KeyDetector::addChroma (const float* mag, int bins, double binHz, std::array<double, 12>& chroma)
{
    for (int k = 1; k < bins; ++k)
    {
        const double f = k * binHz;
        if (f < 50.0 || f > 5000.0)
            continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (midi);
        if (std::abs (midi - nearest) > 0.3)   // only energy close to a real note
            continue;
        const int pc = ((int) nearest % 12 + 12) % 12;
        chroma[(size_t) pc] += std::sqrt ((double) mag[k]) * (f < 250.0 ? 1.5 : 1.0);   // bass defines the key
    }
}

KeyDetector::KeyDetector() : juce::Thread ("OJU beat key")
{
    fifoData.resize ((size_t) fifoSize, 0.0f);
    startThread (juce::Thread::Priority::low);
}

KeyDetector::~KeyDetector()
{
    stopThread (3000);
}

void KeyDetector::push (const float* const* ch, int nch, int n) noexcept
{
    if (nch <= 0) return;
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    const float scale = 1.0f / (float) nch;
    auto write = [&] (int dest, int count, int src)
    {
        for (int i = 0; i < count; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < nch; ++c) v += ch[c][src + i];
            fifoData[(size_t) (dest + i)] = v * scale;
        }
    };
    write (s1, n1, 0);
    write (s2, n2, n1);
    fifo.finishedWrite (n1 + n2);
}

void KeyDetector::run()
{
    ChromaEngine engine;
    std::vector<float> window ((size_t) fftSize, 0.0f);
    std::array<double, 12> chroma {};
    int filled = 0, sinceFrame = 0;
    double heardSeconds = 0.0;

    while (! threadShouldExit())
    {
        if (restartRequested.exchange (false))
        {
            chroma.fill (0.0); filled = 0; sinceFrame = 0; heardSeconds = 0.0;
            key = -1; confidence = 0.0f; heard = 0.0f;
        }

        const int ready = fifo.getNumReady();
        if (ready > 0)
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (ready, s1, n1, s2, n2);
            const double sr = sampleRate.load();
            auto consume = [&] (int start, int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    std::memmove (window.data(), window.data() + 1, sizeof (float) * (size_t) (fftSize - 1));
                    window[(size_t) fftSize - 1] = fifoData[(size_t) (start + i)];
                    filled = juce::jmin (filled + 1, fftSize);
                    if (++sinceFrame >= fftSize / 2 && filled >= fftSize)
                    {
                        sinceFrame = 0;
                        // ~30 s memory, so the key follows the song without jumping around
                        const double decay = std::exp (-(fftSize / 2) / (sr * 30.0));
                        for (auto& c : chroma) c *= decay;
                        double level = 0.0;
                        for (auto v : window) level += (double) v * v;
                        if (level / fftSize > 1.0e-7)   // skip silence
                        {
                            engine.frame (window.data(), sr, chroma);
                            heardSeconds += (fftSize / 2) / sr;
                        }
                    }
                }
            };
            consume (s1, n1);
            consume (s2, n2);
            fifo.finishedRead (n1 + n2);

            if (heardSeconds > 3.0)
            {
                bool m = true;
                float conf = 0.0f;
                const int k = oju::Listener::estimateKey (chroma, m, conf);
                key = k; minor = m;
                confidence = conf * (float) juce::jmin (1.0, heardSeconds / 12.0);
            }
            heard = (float) heardSeconds;
        }
        wait (40);
    }
}

//==============================================================================
bool KeyDetector::parseKeyText (const juce::String& text, int& keyOut, bool& minorOut, bool& modeKnown)
{
    // tokens like "Gm", "G#min", "Bb", "Ebm", "F# minor", "Am", "Cmaj", "D Major" (tags or file names)
    auto quality = [] (const juce::String& q, bool& isMinor) -> bool
    {
        const auto l = q.toLowerCase();
        if (q == "m" || l == "min" || l == "minor" || l == "mi") { isMinor = true;  return true; }
        if (q == "M" || l == "maj" || l == "major" || l == "ma") { isMinor = false; return true; }
        return false;
    };
    static const int letterSemis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G

    const auto norm = text.replace (juce::String::fromUTF8 ("\xe2\x99\xaf"), "#").replace (juce::String::fromUTF8 ("\xe2\x99\xad"), "b");
    juce::StringArray tokens;
    tokens.addTokens (norm, " _-.,()[]{}+&|/\\", "");
    tokens.removeEmptyStrings();

    for (int t = 0; t < tokens.size(); ++t)
    {
        const auto tok = tokens[t];
        const auto letter = juce::CharacterFunctions::toUpperCase (tok[0]);
        if (letter < 'A' || letter > 'G')
            continue;

        auto rest = tok.substring (1);
        int semis = 0;
        bool dummy = true;
        if (rest.startsWith ("#"))
        {
            semis = 1; rest = rest.substring (1);
        }
        else if (rest.startsWith ("b") && (rest.length() == 1 || quality (rest.substring (1), dummy)))
        {
            semis = -1; rest = rest.substring (1);
        }

        bool isMinor = true, known = false;
        if (rest.isNotEmpty())
        {
            if (! quality (rest, isMinor))
                continue;              // "Beat", "Bass", "Amapiano"... not a key
            known = true;
        }
        else if (t + 1 < tokens.size() && quality (tokens[t + 1], isMinor))
        {
            known = true;              // "F# minor"
        }
        else if (semis == 0 && ! (tok.length() == 1 && juce::CharacterFunctions::isUpperCase (tok[0])))
        {
            continue;                  // a lone lower-case letter is just a word
        }

        keyOut = ((letterSemis[letter - 'A'] + semis) % 12 + 12) % 12;
        minorOut = isMinor;
        modeKnown = known;
        return true;
    }
    return false;
}

namespace
{
    // ID3v2 TKEY frame (MP3 files)
    juce::String readId3Key (const juce::File& file)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk()) return {};
        char h[10] {};
        if (in.read (h, 10) != 10 || h[0] != 'I' || h[1] != 'D' || h[2] != '3') return {};
        const int version = h[3];
        const int size = ((h[6] & 0x7f) << 21) | ((h[7] & 0x7f) << 14) | ((h[8] & 0x7f) << 7) | (h[9] & 0x7f);
        juce::MemoryBlock tag;
        if (in.readIntoMemoryBlock (tag, juce::jmin (size, 1 << 20)) <= 0) return {};
        const auto* d = static_cast<const juce::uint8*> (tag.getData());
        size_t pos = 0;
        while (pos + 10 < tag.getSize())
        {
            const juce::String id (reinterpret_cast<const char*> (d + pos), 4);
            if (d[pos] == 0) break;
            const size_t fsize = version >= 4
                ? (size_t) (((d[pos + 4] & 0x7f) << 21) | ((d[pos + 5] & 0x7f) << 14) | ((d[pos + 6] & 0x7f) << 7) | (d[pos + 7] & 0x7f))
                : (size_t) ((d[pos + 4] << 24) | (d[pos + 5] << 16) | (d[pos + 6] << 8) | d[pos + 7]);
            if (fsize == 0 || pos + 10 + fsize > tag.getSize()) break;
            if (id == "TKEY")
                return juce::String::fromUTF8 (reinterpret_cast<const char*> (d + pos + 11), (int) fsize - 1).trim();
            pos += 10 + fsize;
        }
        return {};
    }
}

KeyDetector::FileKey KeyDetector::keyFromFile (const juce::File& file)
{
    FileKey result;
    int k = -1; bool m = true, known = false;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));

    // 1) key tags inside the file
    juce::String tagText = readId3Key (file);
    if (tagText.isEmpty() && reader != nullptr)
        for (auto& name : reader->metadataValues.getAllKeys())
            if (name.containsIgnoreCase ("key") && ! name.containsIgnoreCase ("keyword"))
                tagText = reader->metadataValues[name];
    if (tagText.isNotEmpty() && parseKeyText (tagText, k, m, known))
    {
        result = { k, m, 1.0f, "key tag (" + tagText + ")" };
        if (known) return result;
    }
    if (result.key < 0 && reader != nullptr && reader->metadataValues.getValue ("AcidRootSet", "0") == "1")
    {
        k = reader->metadataValues.getValue ("AcidRootNote", "-1").getIntValue() % 12;
        if (k >= 0) { result = { k, true, 0.9f, "ACID root note" }; known = false; }
    }

    // 2) the file name, e.g. "Vibe 102bpm Gmin.wav"
    if (result.key < 0 && parseKeyText (file.getFileNameWithoutExtension(), k, m, known) && known)
        return { k, m, 0.95f, "file name" };

    // 3) listen to it (also settles major/minor when a tag only gave the root)
    if (reader != nullptr)
    {
        ChromaEngine engine;
        std::array<double, 12> chroma {};
        const int total = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 90.0));
        juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), fftSize);
        std::vector<float> mono ((size_t) fftSize);
        for (int pos = 0; pos + fftSize <= total; pos += fftSize / 2)
        {
            reader->read (&buf, 0, fftSize, pos, true, buf.getNumChannels() > 1);
            for (int i = 0; i < fftSize; ++i)
                mono[(size_t) i] = (buf.getSample (0, i) + buf.getSample (buf.getNumChannels() - 1, i)) * 0.5f;
            engine.frame (mono.data(), reader->sampleRate, chroma);
        }
        bool minorHeard = true;
        float conf = 0.0f;
        const int heardKey = oju::Listener::estimateKey (chroma, minorHeard, conf);
        if (result.key >= 0)
        {
            // the tag gave the root; use the ear for major/minor
            result.minor = minorHeard;
            result.how += ", mode by ear";
            return result;
        }
        return { heardKey, minorHeard, conf, "listened to the file" };
    }
    return result;
}

} // namespace oju
