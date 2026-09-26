// OJU offline renderer, used by the v1 <-> v2 null test.
//
// This file is compiled twice from the same build: once against the frozen OJU v1
// sources (OJURenderV1) and once against the current sources (OJURender). It only
// uses processor API that exists in both versions.
//
//   render   --in take.wav --out out.wav [--block N] [--style N] [--mode N] [--listen]
//            [--save-state file] [--load-state file]
//   compare  --compare a.wav b.wav        (exit 0 only if bit-identical)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"

namespace
{
    juce::String arg (const juce::StringArray& args, const juce::String& name, const juce::String& def = {})
    {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : def;
    }

    bool readWav (const juce::File& f, juce::AudioBuffer<float>& out, double& sr)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
            return false;
        out.setSize ((int) juce::jmin (2u, r->numChannels), (int) r->lengthInSamples);
        r->read (&out, 0, (int) r->lengthInSamples, 0, true, out.getNumChannels() > 1);
        sr = r->sampleRate;
        return true;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                            .withNumChannels (b.getNumChannels()).withBitsPerSample (32)
                                            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    void setParam (oju::OjuProcessor& p, const char* id, float real)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (real));
    }

    int compare (const juce::File& a, const juce::File& b)
    {
        juce::AudioBuffer<float> x, y;
        double sa = 0, sb = 0;
        if (! readWav (a, x, sa) || ! readWav (b, y, sb))
        {
            std::printf ("compare: cannot read files\n");
            return 2;
        }
        if (x.getNumChannels() != y.getNumChannels() || x.getNumSamples() != y.getNumSamples())
        {
            std::printf ("NOT NULL: different shape (%d x %d vs %d x %d)\n",
                         x.getNumChannels(), x.getNumSamples(), y.getNumChannels(), y.getNumSamples());
            return 1;
        }
        double maxDiff = 0.0, peak = 0.0;
        long differing = 0;
        for (int c = 0; c < x.getNumChannels(); ++c)
            for (int i = 0; i < x.getNumSamples(); ++i)
            {
                const float d = x.getSample (c, i) - y.getSample (c, i);
                if (d != 0.0f) ++differing;
                maxDiff = juce::jmax (maxDiff, (double) std::abs (d));
                peak = juce::jmax (peak, (double) std::abs (x.getSample (c, i)));
            }
        if (differing == 0)
        {
            std::printf ("NULL (bit-identical, %d ch x %d samples, peak %.1f dBFS)\n", x.getNumChannels(), x.getNumSamples(),
                         juce::Decibels::gainToDecibels (peak, -200.0));
            return 0;
        }
        std::printf ("NOT NULL: %ld samples differ, max diff %.3g (%.1f dBFS)\n", differing, maxDiff,
                     juce::Decibels::gainToDecibels (maxDiff, -300.0));
        return 1;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.contains ("--compare"))
    {
        const int i = args.indexOf ("--compare");
        return i + 2 < args.size() ? compare (juce::File (args[i + 1]), juce::File (args[i + 2])) : 2;
    }

    juce::AudioBuffer<float> input;
    double sr = 48000.0;
    if (! readWav (juce::File (arg (args, "--in")), input, sr))
    {
        std::printf ("render: cannot read --in file\n");
        return 2;
    }
    const int block = arg (args, "--block", "512").getIntValue();
    const int nch = input.getNumChannels();

    auto makeProcessor = [nch]
    {
        auto proc = std::make_unique<oju::OjuProcessor>();
        const auto set = nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        proc->setBusesLayout (layout);
        return proc;
    };
    juce::MidiBuffer midi;

    // 1) Build the preset: load one, and/or set style/mode, and/or let the brain listen.
    juce::MemoryBlock preset;
    {
        auto setup = makeProcessor();
        auto& p = *setup;
        const auto loadState = arg (args, "--load-state");
        if (loadState.isNotEmpty())
        {
            juce::MemoryBlock mb;
            juce::File (loadState).loadFileAsData (mb);
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            p.pumpMessageThreadWork();
        }
        if (args.contains ("--style")) setParam (p, "style", arg (args, "--style").getFloatValue());
        if (args.contains ("--mode"))  setParam (p, "mode", arg (args, "--mode").getFloatValue());
        p.pumpMessageThreadWork();

        if (args.contains ("--listen"))
        {
            p.setRateAndBufferSizeDetails (sr, block);
            p.prepareToPlay (sr, block);
            p.toggleListening();
            juce::AudioBuffer<float> buf (nch, block);
            int pos = 0;
            for (int guard = 0; guard < 400000; ++guard)
            {
                for (int c = 0; c < nch; ++c)
                    for (int i = 0; i < block; ++i)
                        buf.setSample (c, i, input.getSample (c, (pos + i) % input.getNumSamples()));
                pos = (pos + block) % input.getNumSamples();
                p.processBlock (buf, midi);
                p.pumpMessageThreadWork();
                if ((guard & 7) == 0)
                    juce::Thread::sleep (1);
                const auto st = p.getListenState();
                if (st == oju::Listener::State::done || st == oju::Listener::State::failed)
                    break;
            }
            for (int k = 0; k < 5; ++k)
                p.pumpMessageThreadWork();
            std::printf ("listen: %s\n", p.getListenState() == oju::Listener::State::done ? "done" : "FAILED");
        }
        p.getStateInformation (preset);
    }

    const auto saveState = arg (args, "--save-state");
    if (saveState.isNotEmpty())
        juce::File (saveState).replaceWithData (preset.getData(), preset.getSize());

    // 2) Render from a freshly constructed processor that loads the preset, exactly as a host would.
    auto proc = makeProcessor();
    auto& p = *proc;
    p.setStateInformation (preset.getData(), (int) preset.getSize());
    p.pumpMessageThreadWork();
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);

    // Render the take plus 3 s of silence for tails.
    const int tail = (int) (sr * 3.0);
    const int total = input.getNumSamples() + tail;
    juce::AudioBuffer<float> out (nch, total);
    out.clear();
    for (int c = 0; c < nch; ++c)
        out.copyFrom (c, 0, input, c, 0, input.getNumSamples());

    juce::AudioBuffer<float> buf (nch, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (nch, n, false, false, true);
        for (int c = 0; c < nch; ++c)
            buf.copyFrom (c, 0, out, c, pos, n);
        p.processBlock (buf, midi);
        for (int c = 0; c < nch; ++c)
            out.copyFrom (c, pos, buf, c, 0, n);
    }

    if (! writeWav (juce::File (arg (args, "--out")), out, sr))
    {
        std::printf ("render: cannot write --out file\n");
        return 2;
    }
    std::printf ("rendered %d ch, %.2f s, latency %d samples\n", nch, total / sr, p.getLatencySamples());
    return 0;
}
