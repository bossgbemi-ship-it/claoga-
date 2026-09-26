// Headless test runner for OJU: DSP safety, the Listen brain end-to-end,
// mode/style changes mid-playback, latency, real-time allocation guard,
// CPU cost, state round-trip and editor snapshots.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include "PluginProcessor.h"
#include "ui/PluginEditor.h"

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wmismatched-new-delete", "-Wmissing-prototypes")

//==============================================================================
// Allocation guard: any heap allocation on a thread that is inside processBlock fails the test.
static thread_local bool inAudioCallback = false;
static std::atomic<int> audioThreadAllocations { 0 };

void* operator new (std::size_t size)
{
    if (inAudioCallback)
        audioThreadAllocations.fetch_add (1);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

JUCE_END_IGNORE_WARNINGS_GCC_LIKE

//==============================================================================
namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    }

    // A synthetic "vocal": harmonic source with vibrato and a syllable envelope, a boosted
    // low-mid (mud) around 320 Hz, formants, sibilant bursts around 7 kHz, pauses and room noise.
    struct FakeSinger
    {
        double sr;
        double phase = 0.0, t = 0.0;
        juce::Random rng { 42 };
        oju::SvfState mud, f1, f2, f3, sib1, sib2;
        oju::SvfCoeffs cMud, cF1, cF2, cF3, cSib1, cSib2;
        float level;

        FakeSinger (double rate, float levelDb) : sr (rate), level (juce::Decibels::decibelsToGain (levelDb))
        {
            using namespace oju;
            cMud  = makeSvf (SvfType::bell, sr, 320.0, 1.5, 9.0);
            cF1   = makeSvf (SvfType::bell, sr, 700.0, 2.0, 8.0);
            cF2   = makeSvf (SvfType::bell, sr, 1200.0, 2.0, 6.0);
            cF3   = makeSvf (SvfType::bell, sr, 2800.0, 2.5, 4.0);
            cSib1 = makeSvf (SvfType::highpass, sr, 5500.0, 0.7);
            cSib2 = makeSvf (SvfType::bell, sr, 7200.0, 2.0, 10.0);
        }

        float next()
        {
            const double phraseT = std::fmod (t, 3.0);            // 2.4 s singing, 0.6 s breath
            const bool singing = phraseT < 2.4;
            const double syl = std::fmod (phraseT, 0.4);           // syllables every 400 ms
            float env = singing ? (float) juce::jmin (1.0, syl / 0.03) * (float) juce::jmin (1.0, (0.4 - syl) / 0.08) : 0.0f;
            env *= 0.6f + 0.4f * (float) std::sin (t * 1.3);        // phrase-level dynamics

            // melody gliding over about an octave, with vibrato, so harmonics cover the low mids
            const double f0 = 170.0 * std::pow (2.0, (11.0 / 12.0) * (0.5 + 0.5 * std::sin (t * 1.7) * std::cos (t * 0.43)))
                              * (1.0 + 0.006 * std::sin (t * 2.0 * juce::MathConstants<double>::pi * 5.5));
            phase += f0 / sr;
            phase -= std::floor (phase);

            float src = 0.0f;
            for (int h = 1; h <= 30; ++h)
                if (f0 * h < sr * 0.45)
                    src += (float) std::sin (juce::MathConstants<double>::twoPi * phase * h) / (float) h;

            const float breath = (rng.nextFloat() * 2.0f - 1.0f) * 0.03f;
            float v = f3.process (cF3, f2.process (cF2, f1.process (cF1, mud.process (cMud, src * 0.25f + breath))));

            // sibilant at the start of every other syllable
            const bool sBurst = singing && syl < 0.07 && ((int) (phraseT / 0.4)) % 2 == 0;
            float s = 0.0f;
            if (sBurst)
                s = sib2.process (cSib2, sib1.process (cSib1, rng.nextFloat() * 2.0f - 1.0f)) * 0.5f;

            const float noise = (rng.nextFloat() * 2.0f - 1.0f) * 0.0003f;
            t += 1.0 / sr;
            return (v * env + s) * level + noise;
        }
    };

    struct ScopedAudioCallback
    {
        ScopedAudioCallback()  { inAudioCallback = true; }
        ~ScopedAudioCallback() { inAudioCallback = false; }
    };

    void setParam (oju::OjuProcessor& p, const char* id, float real)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (real));
    }

    float getParam (oju::OjuProcessor& p, const char* id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }

    bool processBlockChecked (oju::OjuProcessor& p, juce::AudioBuffer<float>& b, float& peakOut)
    {
        juce::MidiBuffer midi;
        {
            ScopedAudioCallback guard;
            p.processBlock (b, midi);
        }
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample (c, i);
                if (! std::isfinite (v))
                    return false;
                peakOut = juce::jmax (peakOut, std::abs (v));
            }
        return true;
    }

    std::unique_ptr<oju::OjuProcessor> makeProcessor (double sr, int block, int channels = 2)
    {
        auto p = std::make_unique<oju::OjuProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    bool runListen (oju::OjuProcessor& p, FakeSinger& singer, int block, double timeoutSeconds, float& peak)
    {
        juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
        p.toggleListening();
        const auto start = std::chrono::steady_clock::now();
        bool finite = true;
        while (true)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = singer.next();
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    buf.setSample (c, i, s);
            }
            finite &= processBlockChecked (p, buf, peak);
            // run a bit faster than real time, but let the listener keep up
            juce::Thread::sleep (1);
            p.pumpMessageThreadWork();

            const auto st = p.getListenState();
            if (st == oju::Listener::State::done || st == oju::Listener::State::failed)
                break;
            if (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() > timeoutSeconds)
                break;
        }
        for (int i = 0; i < 5; ++i)
            p.pumpMessageThreadWork();
        return finite;
    }

    void printRead (oju::OjuProcessor& p)
    {
        for (auto& l : p.getReadLines())
            std::printf ("      - %s\n", l.toRawUTF8());
        std::printf ("      Idea: %s\n", p.getIdea().toRawUTF8());
    }
}

//==============================================================================
static void testListenNatural()
{
    std::puts ("\nListen (Natural) on a muddy, sibilant, quiet vocal");
    auto p = makeProcessor (48000.0, 256);
    FakeSinger singer (48000.0, -12.0f);
    float peak = 0.0f;
    const auto t0 = std::chrono::steady_clock::now();
    const bool finite = runListen (*p, singer, 256, 30.0, peak);
    const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    check (finite, "output stays finite while listening");
    check (p->getListenState() == oju::Listener::State::done, "analysis completed (" + juce::String (took, 2) + " s wall clock)");
    check (p->getFeatures().valid, "features valid, voiced seconds = " + juce::String (p->getFeatures().voicedSeconds, 2));
    check (p->getReadLines().size() >= 4 && p->getReadLines().size() <= 6, "The Read has 4-6 lines (" + juce::String (p->getReadLines().size()) + ")");
    check (p->getIdea().isNotEmpty(), "The Read has an idea");
    printRead (*p);

    const auto& f = p->getFeatures();
    std::printf ("      measured: p50 %.1f dB, dyn %.1f dB, crest %.1f dB, mud %.0f Hz (+%.1f), box %.0f Hz (+%.1f), S %.0f Hz (sev %.1f), air %.1f, presence %.1f, harsh %.0f (+%.1f), low voice %.0f Hz\n",
                 f.rmsP50, f.dynamicsDb, f.crestDb, f.mudFreq, f.mudExcessDb, f.boxFreq, f.boxExcessDb, f.sibilanceFreq, f.sibilanceDb,
                 f.airDb, f.presenceDb, f.harshFreq, f.harshExcessDb, f.lowestVoiceHz);

    check (getParam (*p, oju::ids::mudGain) < -0.4f && std::abs (getParam (*p, oju::ids::mudFreq) - 320.0f) < 60.0f,
           "mud cut found near 320 Hz (" + juce::String (getParam (*p, oju::ids::mudFreq)) + " Hz, " + juce::String (getParam (*p, oju::ids::mudGain)) + " dB)");
    check (f.sibilanceFreq > 5500.0f && f.sibilanceFreq < 9000.0f, "S frequency found near 7.2 kHz (" + juce::String (f.sibilanceFreq) + ")");
    check (getParam (*p, oju::ids::inputGain) > 3.0f, "quiet vocal gets input gain (" + juce::String (getParam (*p, oju::ids::inputGain)) + " dB)");
    check (getParam (*p, oju::ids::pressParallel) < 1.0e-6f, "Natural does not use the parallel stage");
    check (audioThreadAllocations.load() == 0, "no heap allocation inside processBlock");
}

static void testListenExtremeAndStyles()
{
    std::puts ("\nListen (Extreme), then switch styles and modes mid-playback");
    auto p = makeProcessor (44100.0, 512);
    setParam (*p, oju::ids::mode, 1.0f);
    setParam (*p, oju::ids::style, 1.0f);   // Trap
    p->pumpMessageThreadWork();
    FakeSinger singer (44100.0, -20.0f);
    float peak = 0.0f;
    const bool finite = runListen (*p, singer, 512, 60.0, peak);
    check (finite, "output stays finite while listening");
    check (p->getListenState() == oju::Listener::State::done, "Extreme analysis completed");
    check (p->getFeatures().voicedSeconds >= 9.9f, "Extreme heard >= 10 s of singing (" + juce::String (p->getFeatures().voicedSeconds, 2) + " s)");
    check (getParam (*p, oju::ids::pressParallel) > 0.0f, "Extreme adds parallel compression");
    printRead (*p);

    // Hammer style/mode/modules while audio runs with ragged block sizes (incl. larger than prepared).
    juce::Random rng (7);
    juce::AudioBuffer<float> buf (2, 4096);
    peak = 0.0f;
    bool ok = true;
    const int sizes[] = { 1, 7, 64, 128, 333, 512, 1024, 4096 };
    for (int iter = 0; iter < 3000; ++iter)
    {
        const int n = sizes[rng.nextInt ((int) std::size (sizes))];
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float s = singer.next() * 4.0f;   // hot input
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        if (iter % 25 == 0) setParam (*p, oju::ids::style, (float) rng.nextInt (5));
        if (iter % 60 == 0) setParam (*p, oju::ids::mode, (float) rng.nextInt (2));
        if (iter % 40 == 0) setParam (*p, oju::ids::delayTime, (float) rng.nextInt (5));
        if (iter % 35 == 0) setParam (*p, oju::ids::heatOn, (float) rng.nextInt (2));
        if (iter % 45 == 0) setParam (*p, oju::ids::spaceOn, (float) rng.nextInt (2));
        if (iter % 90 == 0) setParam (*p, oju::ids::bypass, (float) rng.nextInt (2));
        if (iter % 300 == 0) p->toggleListening();
        if (iter % 10 == 0) p->pumpMessageThreadWork();
        ok &= processBlockChecked (*p, buf, peak);
        if (iter % 700 == 0)
        {
            p->releaseResources();
            p->prepareToPlay (iter % 1400 == 0 ? 96000.0 : 44100.0, 512);
        }
    }
    setParam (*p, oju::ids::bypass, 0.0f);
    check (ok, "3000 ragged blocks with style/mode/module changes: no NaN/Inf");
    check (peak <= 1.0f, "hot input stays under the ceiling (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
    check (audioThreadAllocations.load() == 0, "no heap allocation inside processBlock");
}

static void testLatencyAndAmount()
{
    std::puts ("\nLatency reporting and latency-compensated Amount");
    auto p = makeProcessor (48000.0, 512);
    const int latency = p->getLatencySamples();
    check (latency > 0 && latency < 64, "latency reported to host: " + juce::String (latency) + " samples");
    check (oju::OjuProcessor().getLatencySamples() == latency, "same latency reported before prepareToPlay");

    // Amount 0 -> output must be the (input-gained) dry signal delayed by exactly the reported latency
    setParam (*p, oju::ids::amount, 0.0f);
    setParam (*p, oju::ids::inputGain, 0.0f);
    setParam (*p, oju::ids::ceilingOn, 0.0f);
    juce::AudioBuffer<float> buf (2, 512);
    float peak = 0.0f;
    for (int warm = 0; warm < 20; ++warm) { buf.clear(); processBlockChecked (*p, buf, peak); }
    buf.clear();
    buf.setSample (0, 10, 0.5f);
    buf.setSample (1, 10, 0.5f);
    processBlockChecked (*p, buf, peak);
    int maxIdx = 0;
    for (int i = 0; i < 512; ++i)
        if (std::abs (buf.getSample (0, i)) > std::abs (buf.getSample (0, maxIdx))) maxIdx = i;
    check (maxIdx == 10 + latency && std::abs (buf.getSample (0, maxIdx) - 0.5f) < 1.0e-3f,
           "dry impulse arrives at sample " + juce::String (maxIdx) + " (expected " + juce::String (10 + latency) + ")");

    // Wet path with everything neutral should line up with dry too (no comb filtering at 50%)
    for (auto* id : { oju::ids::tameOn, oju::ids::pressOn, oju::ids::heatOn, oju::ids::spaceOn, oju::ids::eqOn, oju::ids::lowCutOn })
        setParam (*p, id, 0.0f);
    setParam (*p, oju::ids::amount, 100.0f);
    for (int warm = 0; warm < 20; ++warm) { buf.clear(); processBlockChecked (*p, buf, peak); }
    // sine at 1 kHz: wet and dry should be in phase => 50% amount keeps level
    auto rms = [] (juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 256, 256); };
    auto runSine = [&] (float amount)
    {
        setParam (*p, oju::ids::amount, amount);
        double ph = 0.0;
        float r = 0.0f;
        for (int k = 0; k < 30; ++k)
        {
            for (int i = 0; i < 512; ++i) { const float s = 0.25f * (float) std::sin (ph); ph += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0; buf.setSample (0, i, s); buf.setSample (1, i, s); }
            processBlockChecked (*p, buf, peak);
            r = rms (buf);
        }
        return r;
    };
    const float full = runSine (100.0f), half = runSine (50.0f);
    check (std::abs (juce::Decibels::gainToDecibels (half / full)) < 0.5f,
           "wet and dry aligned: 50% amount level change " + juce::String (juce::Decibels::gainToDecibels (half / full), 2) + " dB");
}

static void testCpu()
{
    std::puts ("\nCPU cost (single core, 48 kHz stereo, 256-sample blocks)");
    for (int mode = 0; mode < 2; ++mode)
    {
        auto p = makeProcessor (48000.0, 256);
        setParam (*p, oju::ids::mode, (float) mode);
        setParam (*p, oju::ids::pressParallel, 35.0f);
        FakeSinger singer (48000.0, -18.0f);
        const int blocks = (int) (30.0 * 48000.0 / 256.0);
        juce::AudioBuffer<float> buf (2, 256);
        std::vector<float> src (256);
        juce::MidiBuffer midi;
        double seconds = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 256; ++i) { const float s = singer.next(); buf.setSample (0, i, s); buf.setSample (1, i, s); }
            const auto t0 = std::chrono::steady_clock::now();
            p->processBlock (buf, midi);
            seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        }
        const double pct = seconds / 30.0 * 100.0;
        std::printf ("      %s: %.2f%% of one core (%.0fx real time)\n", mode == 0 ? "Natural" : "Extreme", pct, 30.0 / seconds);
       #if JUCE_DEBUG
        juce::ignoreUnused (pct);
       #else
        check (pct < 8.0, juce::String (mode == 0 ? "Natural" : "Extreme") + " is light enough for many instances");
       #endif
    }
}

static void testStateRoundTrip()
{
    std::puts ("\nState save / restore");
    auto p = makeProcessor (48000.0, 256);
    FakeSinger singer (48000.0, -16.0f);
    float peak = 0.0f;
    runListen (*p, singer, 256, 30.0, peak);
    p->toggleAB();
    setParam (*p, oju::ids::heatDrive, 11.0f);   // B differs from A
    juce::MemoryBlock mb;
    p->getStateInformation (mb);

    auto q = makeProcessor (48000.0, 256);
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    q->pumpMessageThreadWork();
    bool same = true;
    for (auto& id : oju::soundParameterIds())
        same &= std::abs (getParam (*p, id.toRawUTF8()) - getParam (*q, id.toRawUTF8())) < 1.0e-3f;
    check (same, "all parameters restored");
    check (q->getReadLines() == p->getReadLines() && q->getIdea() == p->getIdea(), "The Read restored");
    check (q->getFeatures().valid, "analysis restored (styles can be re-applied without listening)");
    check (q->getActiveSlot() == 1, "A/B slot restored");
    const float heatB = getParam (*q, oju::ids::heatDrive);
    q->toggleAB();
    check (std::abs (getParam (*q, oju::ids::heatDrive) - heatB) > 1.0f, "A/B recalls the other setting");

    // Changing style after a reload re-targets from the stored read
    const float before = getParam (*q, oju::ids::reverbMix);
    setParam (*q, oju::ids::style, 2.0f);   // R&B
    q->pumpMessageThreadWork();
    check (std::abs (getParam (*q, oju::ids::reverbMix) - before) > 0.5f, "style change re-targets settings");
}

static void testMono()
{
    std::puts ("\nMono track");
    auto p = makeProcessor (48000.0, 128, 1);
    FakeSinger singer (48000.0, -14.0f);
    float peak = 0.0f;
    const bool finite = runListen (*p, singer, 128, 30.0, peak);
    check (finite && p->getListenState() == oju::Listener::State::done, "mono listen + processing works");
}

static void testEditor (const juce::File& outDir)
{
    std::puts ("\nEditor snapshots");
    auto p = makeProcessor (48000.0, 256);
    FakeSinger singer (48000.0, -14.0f);
    float peak = 0.0f;
    runListen (*p, singer, 256, 30.0, peak);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    check (ed != nullptr, "editor created");
    for (auto size : { std::make_pair (1280, 820), std::make_pair (800, 512), std::make_pair (1920, 1230) })
    {
        ed->setSize (size.first, size.second);
        // feed a little audio so meters and the eye move
        juce::AudioBuffer<float> buf (2, 256);
        for (int k = 0; k < 20; ++k)
        {
            for (int i = 0; i < 256; ++i) { const float s = singer.next(); buf.setSample (0, i, s); buf.setSample (1, i, s); }
            juce::MidiBuffer midi;
            p->processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto file = outDir.getChildFile ("oju_" + juce::String (size.first) + "x" + juce::String (size.second) + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat png;
        check (os.openedOk() && png.writeImageToStream (img, os), "snapshot " + file.getFileName());
    }

    // Module detail views
    if (auto* oe = dynamic_cast<oju::OjuEditor*> (ed.get()))
    {
        ed->setSize (1280, 820);
        for (int m : { 0, 2, 3, 5, 10, 11 })
        {
            oe->showModule (m);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto file = outDir.getChildFile ("oju_module_" + juce::String (m + 1) + ".png");
            file.deleteFile();
            juce::FileOutputStream mos (file);
            juce::PNGImageFormat().writeImageToStream (img, mos);
        }
        oe->showModule (13 - 1);   // back to The Read
    }

    // Listening state snapshot
    p->toggleListening();
    for (int k = 0; k < 150; ++k)
    {
        juce::AudioBuffer<float> buf (2, 256);
        for (int i = 0; i < 256; ++i) { const float s = singer.next(); buf.setSample (0, i, s); buf.setSample (1, i, s); }
        juce::MidiBuffer midi;
        p->processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3);
    }
    ed->setSize (1280, 820);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::FileOutputStream os (outDir.getChildFile ("oju_listening.png"));
    os.setPosition (0); os.truncate();
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
}


//==============================================================================
// OJU 2.0 module tests
namespace v2
{
    juce::AudioBuffer<float> singer (double sr, float levelDb, double seconds, int chans = 2)
    {
        FakeSinger fs (sr, levelDb);
        juce::AudioBuffer<float> b (chans, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = fs.next();
            for (int c = 0; c < chans; ++c)
                b.setSample (c, i, v);
        }
        return b;
    }

    juce::AudioBuffer<float> run (oju::OjuProcessor& p, const juce::AudioBuffer<float>& in, int block, bool& finite)
    {
        juce::AudioBuffer<float> out (in);
        juce::AudioBuffer<float> buf (in.getNumChannels(), block);
        float peak = 0.0f;
        for (int pos = 0; pos < in.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, in.getNumSamples() - pos);
            buf.setSize (in.getNumChannels(), n, false, false, true);
            for (int c = 0; c < in.getNumChannels(); ++c)
                buf.copyFrom (c, 0, in, c, pos, n);
            finite &= processBlockChecked (p, buf, peak);
            for (int c = 0; c < in.getNumChannels(); ++c)
                out.copyFrom (c, pos, buf, c, 0, n);
            if ((pos / block) % 8 == 0)
                p.pumpMessageThreadWork();
        }
        return out;
    }

    float rmsDb (const juce::AudioBuffer<float>& b, int start, int len, int ch = 0)
    {
        start = juce::jlimit (0, b.getNumSamples() - 1, start);
        len = juce::jlimit (1, b.getNumSamples() - start, len);
        return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, start, len), -200.0f);
    }

    float peakDb (const juce::AudioBuffer<float>& b)
    {
        float pk = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            pk = juce::jmax (pk, b.getMagnitude (c, 0, b.getNumSamples()));
        return juce::Decibels::gainToDecibels (pk, -200.0f);
    }

    // Everything off, so a test hears only the module it switches on.
    void neutral (oju::OjuProcessor& p)
    {
        for (auto* id : { oju::ids::lowCutOn, oju::ids::eqOn, oju::ids::tameOn, oju::ids::pressOn, oju::ids::heatOn,
                          oju::ids::spaceOn, oju::ids::ceilingOn })
            setParam (p, id, 0.0f);
        setParam (p, oju::ids::inputGain, 0.0f);
        setParam (p, oju::ids::outputGain, 0.0f);
        setParam (p, oju::ids::amount, 100.0f);
        p.pumpMessageThreadWork();
    }

    float bandRmsDb (const juce::AudioBuffer<float>& b, double sr, oju::SvfType type, double f, int start, int len)
    {
        auto c = oju::makeSvf (type, sr, f, 0.707);
        oju::SvfState st;
        double sum = 0.0;
        for (int i = 0; i < start + len && i < b.getNumSamples(); ++i)
        {
            const float y = st.process (c, b.getSample (0, i));
            if (i >= start) sum += (double) y * y;
        }
        return (float) (10.0 * std::log10 (sum / juce::jmax (1, len) + 1.0e-20));
    }
}

static void testPhase1Modules()
{
    std::puts ("\nOJU 2.0 phase 1: Cleanup, De-esser, Delay, Reverb, Output, Track mode");
    const double sr = 48000.0;
    bool finite = true;

    // ---- Plosive tamer: a 45 Hz pop burst inside the vocal
    {
        auto in = v2::singer (sr, -18.0f, 3.0);
        const int popAt = (int) (1.03 * sr);
        for (int i = 0; i < (int) (0.08 * sr); ++i)
        {
            const float t = (float) i / (float) sr;
            const float pop = 0.6f * std::exp (-t / 0.025f) * std::sin (juce::MathConstants<float>::twoPi * 45.0f * t);
            for (int c = 0; c < 2; ++c)
                in.setSample (c, popAt + i, in.getSample (c, popAt + i) + pop);
        }
        auto pOff = makeProcessor (sr, 256); v2::neutral (*pOff);
        auto pOn = makeProcessor (sr, 256);  v2::neutral (*pOn);
        setParam (*pOn, oju::ids::plosiveOn, 1.0f);
        setParam (*pOn, oju::ids::plosiveAmount, 70.0f);
        const auto a = v2::run (*pOff, in, 256, finite);
        const auto b = v2::run (*pOn, in, 256, finite);
        const float popOff = v2::bandRmsDb (a, sr, oju::SvfType::lowpass, 150.0, popAt, (int) (0.08 * sr));
        const float popOn  = v2::bandRmsDb (b, sr, oju::SvfType::lowpass, 150.0, popAt, (int) (0.08 * sr));
        const float midOff = v2::bandRmsDb (a, sr, oju::SvfType::bandpass, 1000.0, (int) (0.2 * sr), (int) (0.6 * sr));
        const float midOn  = v2::bandRmsDb (b, sr, oju::SvfType::bandpass, 1000.0, (int) (0.2 * sr), (int) (0.6 * sr));
        check (popOff - popOn > 4.0f, "plosive tamer: pop below 150 Hz down " + juce::String (popOff - popOn, 1) + " dB");
        check (std::abs (midOff - midOn) < 0.5f, "plosive tamer leaves the voice alone (" + juce::String (midOn - midOff, 2) + " dB mids)");
    }

    // ---- Safety limiter: hot input, ceiling -1 dB, lookahead latency in Mix, none in Track
    for (int track = 0; track < 2; ++track)
    {
        auto in = v2::singer (sr, -2.0f, 2.0);
        in.applyGain (4.0f);
        auto p = makeProcessor (sr, 256); v2::neutral (*p);
        setParam (*p, oju::ids::limiterOn, 1.0f);
        setParam (*p, oju::ids::latencyMode, (float) track);
        p->pumpMessageThreadWork();
        const auto out = v2::run (*p, in, 256, finite);
        const float pk = v2::peakDb (out);
        check (pk <= -0.99f, juce::String (track ? "Track" : "Mix") + " limiter holds the -1 dB ceiling (peak " + juce::String (pk, 2) + " dBFS)");
        const int expected = track ? 0 : 4 + (int) std::ceil (0.0015 * sr);
        check (p->getLatencySamples() == expected, juce::String (track ? "Track" : "Mix") + " mode latency " + juce::String (p->getLatencySamples())
                                                   + " samples (expected " + juce::String (expected) + ")");
    }

    // ---- Track mode really is zero latency: an impulse comes straight through
    {
        auto p = makeProcessor (sr, 256); v2::neutral (*p);
        setParam (*p, oju::ids::latencyMode, 1.0f);
        setParam (*p, oju::ids::heatOn, 1.0f);
        p->pumpMessageThreadWork();
        juce::AudioBuffer<float> in (2, 2048);
        in.clear();
        in.setSample (0, 100, 0.5f); in.setSample (1, 100, 0.5f);
        const auto out = v2::run (*p, in, 256, finite);
        int maxIdx = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, maxIdx))) maxIdx = i;
        check (maxIdx == 100, "Track mode: zero latency, Heat at 1x (impulse at sample " + juce::String (maxIdx) + ")");
    }

    // ---- De-esser auto band follows the S's (the fake singer's S's sit around 7.2 kHz)
    {
        auto in = v2::singer (sr, -16.0f, 4.0);
        auto p = makeProcessor (sr, 256); v2::neutral (*p);
        setParam (*p, oju::ids::tameOn, 1.0f);
        setParam (*p, oju::ids::tameAmount, 60.0f);
        setParam (*p, oju::ids::tameFreq, 4000.0f);
        setParam (*p, oju::ids::deessAuto, 1.0f);
        p->pumpMessageThreadWork();
        v2::run (*p, in, 256, finite);
        const float band = p->getChain().tameBandHz.load();
        check (band > 5000.0f && band < 7500.0f, "auto de-esser moved its split to " + juce::String (band, 0) + " Hz (S's at 7.2 kHz)");
    }

    // ---- Delay ducks while singing
    {
        auto in = v2::singer (sr, -16.0f, 3.0);
        auto render = [&] (float duck)
        {
            auto p = makeProcessor (sr, 256); v2::neutral (*p);
            setParam (*p, oju::ids::spaceOn, 1.0f);
            setParam (*p, oju::ids::verbOn, 0.0f);
            setParam (*p, oju::ids::delayMix, 100.0f);
            setParam (*p, oju::ids::delayFeedback, 0.0f);
            setParam (*p, oju::ids::echoDuck, duck);
            p->pumpMessageThreadWork();
            return v2::run (*p, in, 256, finite);
        };
        const auto dry = [&] { auto p = makeProcessor (sr, 256); v2::neutral (*p); return v2::run (*p, in, 256, finite); }();
        const auto a = render (0.0f), b = render (100.0f);
        // echo = output - dry; measure its energy while singing (first phrase)
        auto echoDb = [&] (const juce::AudioBuffer<float>& o)
        {
            double sum = 0.0; const int s0 = (int) (0.5 * sr), s1 = (int) (2.3 * sr);
            for (int i = s0; i < s1; ++i) { const double d = o.getSample (0, i) - dry.getSample (0, i); sum += d * d; }
            return (float) (10.0 * std::log10 (sum / (s1 - s0) + 1.0e-20));
        };
        check (echoDb (a) - echoDb (b) > 8.0f, "delay ducks while singing (echo " + juce::String (echoDb (a) - echoDb (b), 1) + " dB lower)");
    }

    // ---- Reverb types each leave a tail
    {
        auto in = v2::singer (sr, -16.0f, 2.4);
        juce::AudioBuffer<float> padded (2, in.getNumSamples() + (int) (1.5 * sr));
        padded.clear();
        for (int c = 0; c < 2; ++c) padded.copyFrom (c, 0, in, c, 0, in.getNumSamples());
        float tails[4] {};
        for (int t = 0; t < 4; ++t)
        {
            auto p = makeProcessor (sr, 256); v2::neutral (*p);
            setParam (*p, oju::ids::spaceOn, 1.0f);
            setParam (*p, oju::ids::echoOn, 0.0f);
            setParam (*p, oju::ids::reverbMix, 40.0f);
            setParam (*p, oju::ids::verbType, (float) t);
            p->pumpMessageThreadWork();
            const auto out = v2::run (*p, padded, 256, finite);
            tails[t] = v2::rmsDb (out, in.getNumSamples() + (int) (0.3 * sr), (int) (0.3 * sr));
        }
        check (tails[0] > -80.0f && tails[1] > -80.0f && tails[2] > -80.0f && tails[3] > -80.0f,
               "reverb classic/room/plate/hall tails: " + juce::String (tails[0], 0) + " / " + juce::String (tails[1], 0) + " / "
               + juce::String (tails[2], 0) + " / " + juce::String (tails[3], 0) + " dB");
        check (tails[3] > tails[1], "hall rings longer than room");
    }

    // ---- Vocal rider evens out a quiet verse and a loud hook
    {
        auto quiet = v2::singer (sr, -28.0f, 6.0), loud = v2::singer (sr, -12.0f, 6.0);
        juce::AudioBuffer<float> in (2, quiet.getNumSamples() + loud.getNumSamples());
        for (int c = 0; c < 2; ++c) { in.copyFrom (c, 0, quiet, c, 0, quiet.getNumSamples()); in.copyFrom (c, quiet.getNumSamples(), loud, c, 0, loud.getNumSamples()); }
        auto measure = [&] (bool rider)
        {
            auto p = makeProcessor (sr, 256); v2::neutral (*p);
            setParam (*p, oju::ids::riderOn, rider ? 1.0f : 0.0f);
            setParam (*p, oju::ids::riderAmount, 100.0f);
            p->pumpMessageThreadWork();
            const auto out = v2::run (*p, in, 256, finite);
            const int half = quiet.getNumSamples();
            return v2::rmsDb (out, half + (int) (3 * sr), (int) (2.5 * sr)) - v2::rmsDb (out, (int) (3 * sr), (int) (2.5 * sr));
        };
        const float spreadOff = measure (false), spreadOn = measure (true);
        check (spreadOn < spreadOff - 4.0f, "rider evens the level: verse-to-hook jump " + juce::String (spreadOff, 1)
                                            + " dB -> " + juce::String (spreadOn, 1) + " dB");
    }

    // ---- "v1" compare reproduces the v1 sound exactly, whatever 2.0 modules are on
    {
        auto in = v2::singer (sr, -16.0f, 2.0);
        auto ref = makeProcessor (sr, 256);
        auto cmp = makeProcessor (sr, 256);
        for (auto* id : { oju::ids::plosiveOn, oju::ids::limiterOn, oju::ids::riderOn, oju::ids::deessAuto })
            setParam (*cmp, id, 1.0f);
        setParam (*cmp, oju::ids::echoDuck, 80.0f);
        setParam (*cmp, oju::ids::verbType, 3.0f);
        cmp->setV1Compare (true);
        cmp->pumpMessageThreadWork();
        const auto a = v2::run (*ref, in, 256, finite);
        const auto b = v2::run (*cmp, in, 256, finite);
        bool same = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < a.getNumSamples(); ++i)
                same &= juce::exactlyEqual (a.getSample (c, i), b.getSample (c, i));
        check (same, "v1 compare button: bit-identical to OJU v1");
    }

    check (finite, "all phase 1 renders finite");
    check (audioThreadAllocations.load() == 0, "no heap allocation inside processBlock");
}

static void testPhase2Modules()
{
    std::puts ("\nOJU 2.0 phase 2: Compress (leveler), Breath control, Double + Width");
    const double sr = 48000.0;
    bool finite = true;

    // ---- Leveler: evens a quiet verse and a loud hook before the v1 Press
    {
        auto quiet = v2::singer (sr, -30.0f, 6.0), loud = v2::singer (sr, -12.0f, 6.0);
        juce::AudioBuffer<float> in (2, quiet.getNumSamples() + loud.getNumSamples());
        for (int c = 0; c < 2; ++c) { in.copyFrom (c, 0, quiet, c, 0, quiet.getNumSamples()); in.copyFrom (c, quiet.getNumSamples(), loud, c, 0, loud.getNumSamples()); }
        auto spread = [&] (bool on)
        {
            auto p = makeProcessor (sr, 256); v2::neutral (*p);
            setParam (*p, oju::ids::levelOn, on ? 1.0f : 0.0f);
            setParam (*p, oju::ids::levelAmount, 100.0f);
            p->pumpMessageThreadWork();
            const auto out = v2::run (*p, in, 256, finite);
            const int half = quiet.getNumSamples();
            return v2::rmsDb (out, half + (int) (3 * sr), (int) (2.5 * sr)) - v2::rmsDb (out, (int) (3 * sr), (int) (2.5 * sr));
        };
        const float off = spread (false), on = spread (true);
        check (on < off - 3.0f, "leveler (auto threshold) evens verse/hook: " + juce::String (off, 1) + " dB -> " + juce::String (on, 1) + " dB");
    }

    // ---- Breath control: breaths in the gaps go down, the singing doesn't
    {
        auto in = v2::singer (sr, -14.0f, 6.0);
        juce::Random rng (5);
        auto bp = oju::makeSvf (oju::SvfType::bandpass, sr, 1800.0, 0.8);
        oju::SvfState st;
        // the fake singer breathes 2.45-2.95 s and 5.45-5.95 s (between phrases)
        for (double t0 : { 2.45, 5.45 })
            for (int i = 0; i < (int) (0.5 * sr); ++i)
            {
                const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) (0.5 * sr));
                const float b = st.process (bp, rng.nextFloat() * 2.0f - 1.0f) * 0.06f * env;
                const int idx = (int) (t0 * sr) + i;
                for (int c = 0; c < 2; ++c) in.setSample (c, idx, in.getSample (c, idx) + b);
            }
        auto render = [&] (bool on)
        {
            auto p = makeProcessor (sr, 256); v2::neutral (*p);
            setParam (*p, oju::ids::breathOn, on ? 1.0f : 0.0f);
            setParam (*p, oju::ids::breathAmount, 100.0f);
            p->pumpMessageThreadWork();
            return v2::run (*p, in, 256, finite);
        };
        const auto a = render (false), b = render (true);
        const int br = (int) (2.6 * sr), brLen = (int) (0.25 * sr);
        const float breathDrop = v2::rmsDb (a, br, brLen) - v2::rmsDb (b, br, brLen);
        const float sungDrop = v2::rmsDb (a, (int) (0.3 * sr), (int) (2.0 * sr)) - v2::rmsDb (b, (int) (0.3 * sr), (int) (2.0 * sr));
        // sustained note tail: the last 150 ms of the phrase must survive
        const float tailDrop = v2::rmsDb (a, (int) (2.22 * sr), (int) (0.15 * sr)) - v2::rmsDb (b, (int) (2.22 * sr), (int) (0.15 * sr));
        check (breathDrop > 8.0f, "breaths turned down " + juce::String (breathDrop, 1) + " dB (not removed)");
        check (breathDrop < 19.0f, "breaths kept, not deleted (max 18 dB)");
        check (sungDrop < 0.5f, "singing untouched (" + juce::String (sungDrop, 2) + " dB)");
        check (tailDrop < 1.0f, "note tails untouched (" + juce::String (tailDrop, 2) + " dB)");
    }

    // ---- Double + Width: a mono vocal becomes a wide stereo double
    {
        auto in = v2::singer (sr, -16.0f, 4.0);
        auto sideToMid = [&] (const juce::AudioBuffer<float>& o, int start, int len)
        {
            double m = 0.0, sd = 0.0;
            for (int i = start; i < start + len; ++i)
            {
                const double l = o.getSample (0, i), r = o.getSample (1, i);
                m += (l + r) * (l + r); sd += (l - r) * (l - r);
            }
            return (float) (10.0 * std::log10 ((sd + 1.0e-20) / (m + 1.0e-20)));
        };
        auto p = makeProcessor (sr, 256); v2::neutral (*p);
        setParam (*p, oju::ids::doubleOn, 1.0f);
        setParam (*p, oju::ids::doubleAmount, 50.0f);
        setParam (*p, oju::ids::width, 60.0f);
        p->pumpMessageThreadWork();
        const auto out = v2::run (*p, in, 256, finite);
        const float sm = sideToMid (out, (int) (0.5 * sr), (int) (1.5 * sr));
        check (sm > -20.0f, "double makes a mono vocal stereo (side/mid " + juce::String (sm, 1) + " dB)");
    }

    // ---- Hook only: the double comes in on the loud section, not the quiet verse
    {
        auto quiet = v2::singer (sr, -28.0f, 8.0), loud = v2::singer (sr, -14.0f, 8.0);
        juce::AudioBuffer<float> in (2, quiet.getNumSamples() + loud.getNumSamples());
        for (int c = 0; c < 2; ++c) { in.copyFrom (c, 0, quiet, c, 0, quiet.getNumSamples()); in.copyFrom (c, quiet.getNumSamples(), loud, c, 0, loud.getNumSamples()); }
        auto p = makeProcessor (sr, 256); v2::neutral (*p);
        setParam (*p, oju::ids::doubleOn, 1.0f);
        setParam (*p, oju::ids::doubleAmount, 60.0f);
        setParam (*p, oju::ids::hookOnly, 1.0f);
        p->pumpMessageThreadWork();
        const auto out = v2::run (*p, in, 256, finite);
        auto sideDb = [&] (int start, int len)
        {
            double sd = 0.0, m = 0.0;
            for (int i = start; i < start + len; ++i)
            {
                const double l = out.getSample (0, i), r = out.getSample (1, i);
                sd += (l - r) * (l - r); m += (l + r) * (l + r);
            }
            return (float) (10.0 * std::log10 ((sd + 1.0e-20) / (m + 1.0e-20)));
        };
        const float verse = sideDb ((int) (5 * sr), (int) (2 * sr));
        const float hook = sideDb (quiet.getNumSamples() + (int) (2 * sr), (int) (3 * sr));
        check (hook > verse + 10.0f, "hook only: double in the hook (" + juce::String (hook, 1) + " dB side) not the verse ("
                                     + juce::String (verse, 1) + " dB)");
    }

    check (finite, "all phase 2 renders finite");
    check (audioThreadAllocations.load() == 0, "no heap allocation inside processBlock");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    // --write-vocal <file.wav> <seconds>: render the synthetic singer, for live-input testing
    if (argc > 3 && juce::String (argv[1]) == "--write-vocal")
    {
        const double sr = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 48000.0;
        const int chans = argc > 5 ? juce::jlimit (1, 2, juce::String (argv[5]).getIntValue()) : 1;
        FakeSinger singer (sr, -16.0f);
        const int n = (int) (juce::String (argv[3]).getDoubleValue() * sr);
        juce::AudioBuffer<float> b (chans, n);
        float r = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float s = singer.next();
            b.setSample (0, i, s);
            if (chans > 1)
            {
                r = 0.7f * r + 0.3f * s;   // a slightly different, filtered right channel
                b.setSample (1, i, r * 0.9f);
            }
        }
        const juce::File f { juce::String (argv[2]) };
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (chans).withBitsPerSample (24));
        if (writer != nullptr && writer->writeFromAudioSampleBuffer (b, 0, n))
            return 0;
        return 1;
    }

    const auto outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();

    std::puts ("OJU test runner");
    testListenNatural();
    testListenExtremeAndStyles();
    testLatencyAndAmount();
    testCpu();
    testStateRoundTrip();
    testMono();
    testPhase1Modules();
    testPhase2Modules();
    if (juce::SystemStats::getEnvironmentVariable ("OJU_SKIP_EDITOR", {}).isEmpty())
        testEditor (outDir);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
