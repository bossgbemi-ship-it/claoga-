#include "PluginEditor.h"
#include "Layout.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace oju
{
using namespace theme;

namespace
{
    juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }

    constexpr int cellW = 104, cellH = 150;

    juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }
}

//==============================================================================
OjuEditor::OjuEditor (OjuProcessor& p)
    : AudioProcessorEditor (p),
      ojuProcessor (p),
      roleChoice (p.getState(), ids::role),
      genreChoice (p.getState(), ids::style, { genreStyles().begin(), genreStyles().end() }),
      modeChoice (p.getState(), ids::mode),
      latencyChoice (p.getState(), ids::latencyMode)
{
    setLookAndFeel (&lookAndFeel);
    auto& state = p.getState();

    addAndMakeVisible (content);
    content.setBounds (0, 0, designWidth, designHeight);
    content.addAndMakeVisible (backplate);
    backplate.setBounds (content.getLocalBounds());

    // ---- top bar
    for (auto* c : std::initializer_list<juce::Component*> { &roleChoice, &genreChoice, &modeChoice })
        content.addAndMakeVisible (c);
    roleChoice.setBounds (layout::roleChoice);
    genreChoice.setBounds (layout::genreChoice);
    modeChoice.setBounds (layout::modeChoice);

    presetsButton.onClick = [this] { showPresetsMenu(); };
    presetsButton.setTooltip ("Genre presets, and your own saved presets");
    content.addAndMakeVisible (presetsButton);
    presetsButton.setBounds (layout::presetsBtn);

    abButton.textProvider = [this] { return ojuProcessor.getActiveSlot() == 0 ? juce::String ("A|b") : juce::String ("a|B"); };
    abButton.onClick = [this] { ojuProcessor.toggleAB(); };
    abButton.setTooltip ("Compare two settings");
    content.addAndMakeVisible (abButton);
    abButton.setBounds (layout::abButton);

    v1Button.setClickingTogglesState (true);
    v1Button.litColour = patina;
    v1Button.onClick = [this] { ojuProcessor.setV1Compare (v1Button.getToggleState()); };
    v1Button.setTooltip ("Hear the plain OJU v1 sound (all 2.0 modules off)");
    content.addAndMakeVisible (v1Button);
    v1Button.setBounds (layout::v1Button);

    bypassButton.setClickingTogglesState (true);
    bypassButton.litColour = oxide.brighter (0.2f);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ids::bypass, bypassButton);
    content.addAndMakeVisible (bypassButton);
    bypassButton.setBounds (layout::bypassBtn);

    // ---- AUTO eye
    eye.onClick = [this] { eyeClicked(); };
    content.addAndMakeVisible (eye);
    eye.setBounds (layout::eye());
    content.addAndMakeVisible (eyeStatus);
    eyeStatus.setBounds (layout::eyeStatus());
    eyeStatus.setInterceptsMouseClicks (false, false);

    // ---- detail panel
    content.addAndMakeVisible (detail);
    detail.setBounds (layout::detailPanel.reduced (8));
    detail.onToggle = [this] (int id) { toggleModule (id); };
    detail.onShowRead = [this] { selectModule (DetailPanel::readView); };

    // ---- status column
    {
        auto m = layout::meters();
        const int w = m.getWidth() / 3;
        for (auto* meter : { &inMeter, &grMeter, &outMeter })
        {
            content.addAndMakeVisible (*meter);
            meter->setBounds (m.removeFromLeft (w));
        }
    }
    content.addAndMakeVisible (latencyChoice);
    latencyChoice.setBounds (layout::latencyChoice());
    content.addAndMakeVisible (statusInfo);
    statusInfo.setBounds (layout::statusInfo());
    statusInfo.setInterceptsMouseClicks (false, false);

    // ---- rack
    for (int i = 0; i < numModules; ++i)
    {
        tiles[(size_t) i] = std::make_unique<ModuleTile> (i);
        auto& t = *tiles[(size_t) i];
        t.onSelect = [this] (int id) { selectModule (detail.current() == id ? DetailPanel::readView : id); };
        t.onToggle = [this] (int id) { toggleModule (id); };
        content.addAndMakeVisible (t);
        t.setBounds (layout::tile (i));
    }

    buildViews();

    // ---- window: resizable, fixed aspect, vector-scaled
    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (juce::roundToInt (designWidth * 0.9), juce::roundToInt (designHeight * 0.9));

    timerCallback();
    startTimerHz (30);
}

OjuEditor::~OjuEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Rectangle<int> OjuEditor::cell (int col, int row, int colSpan, int x0)
{
    return { x0 + col * cellW, row * cellH, cellW * colSpan, cellH };
}

Knob& OjuEditor::addKnob (juce::Component& parent, const char* id, const juce::String& name, int col, int row,
                          bool bipolar, bool stepped)
{
    auto k = std::make_unique<Knob> (ojuProcessor.getState(), id, name, bipolar, stepped);
    parent.addAndMakeVisible (*k);
    k->setBounds (cell (col, row).reduced (4));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

LampSwitch& OjuEditor::addSwitch (juce::Component& parent, const char* id, const juce::String& label, juce::Rectangle<int> bounds)
{
    auto s = std::make_unique<LampSwitch> (ojuProcessor.getState(), id, label);
    parent.addAndMakeVisible (*s);
    s->setBounds (bounds);
    switches.push_back (std::move (s));
    return *switches.back();
}

void OjuEditor::buildViews()
{
    auto& state = ojuProcessor.getState();

    auto makeReadout = [this] (juce::Component& parent, const juce::String& name, juce::Rectangle<int> bounds) -> Readout*
    {
        auto r = std::make_unique<Readout>();
        r->name = name;
        r->setInterceptsMouseClicks (false, false);
        parent.addAndMakeVisible (*r);
        r->setBounds (bounds);
        auto* raw = r.get();
        extras.push_back (std::move (r));
        return raw;
    };

    auto makeButton = [this] (juce::Component& parent, const juce::String& text, juce::Rectangle<int> bounds) -> PlateButton*
    {
        auto b = std::make_unique<PlateButton> (text);
        parent.addAndMakeVisible (*b);
        b->setBounds (bounds);
        auto* raw = b.get();
        extras.push_back (std::move (b));
        return raw;
    };

    auto makeChoice = [this, &state] (juce::Component& parent, const char* id, juce::Rectangle<int> bounds)
    {
        auto c = std::make_unique<SegmentedChoice> (state, id);
        parent.addAndMakeVisible (*c);
        c->setBounds (bounds);
        extras.push_back (std::move (c));
    };

    // 1 Denoise
    {
        auto& v = detail.view (mDenoise);
        addKnob (v, ids::denoiseAmount, "Amount", 0, 0);
        addKnob (v, ids::roomAmount, "Room", 1, 0);
        learnRoomButton = makeButton (v, "Learn room", { 240, 20, 150, 44 });
        learnRoomButton->setTooltip ("Stay quiet for 2 seconds: OJU fingerprints the room noise");
        learnRoomButton->onClick = [this] { ojuProcessor.learnRoom(); };
        denoiseInfo = makeReadout (v, "Room", { 240, 74, 390, 70 });
    }

    // 2 Cleanup
    {
        auto& v = detail.view (mCleanup);
        addKnob (v, ids::lowCutFreq, "High-pass", 0, 0);
        addKnob (v, ids::plosiveAmount, "Plosives", 1, 0);
        addSwitch (v, ids::lowCutOn, "High-pass", { 230, 24, 150, 30 });
        addSwitch (v, ids::plosiveOn, "Plosive tamer", { 230, 64, 170, 30 });
        cleanupInfo = makeReadout (v, "Voice", { 230, 104, 400, 70 });
    }

    // 3 Tune
    {
        auto& v = detail.view (mTune);
        makeChoice (v, ids::tuneKeySource, { 4, 4, 200, 34 });
        auto shift = [] (juce::Rectangle<int> r) { return r.translated (0, 42); };
        auto k1 = &addKnob (v, ids::tuneKey, "Key", 0, 0, false, true);   k1->setBounds (shift (cell (0, 0)).reduced (4).withHeight (126));
        auto k2 = &addKnob (v, ids::tuneScale, "Scale", 1, 0, false, true); k2->setBounds (shift (cell (1, 0)).reduced (4).withHeight (126));
        auto k3 = &addKnob (v, ids::tuneSpeed, "Retune", 2, 0);            k3->setBounds (shift (cell (2, 0)).reduced (4).withHeight (126));
        auto k4 = &addKnob (v, ids::tuneHumanize, "Humanize", 3, 0);       k4->setBounds (shift (cell (3, 0)).reduced (4).withHeight (126));
        auto k5 = &addKnob (v, ids::tuneMix, "Amount", 4, 0);              k5->setBounds (shift (cell (4, 0)).reduced (4).withHeight (126));
        loadBeatButton = makeButton (v, "Key from beat file", { 4, 182, 200, 40 });
        loadBeatButton->setTooltip ("Pick your beat: OJU reads its key tag, or listens to it");
        tuneInfo = makeReadout (v, "Key", { 220, 176, 420, 70 });
    }

    // 4 EQ (the v1 Shape EQ)
    {
        auto& v = detail.view (mEq);
        v.addAndMakeVisible (eqCurve);
        eqCurve.setBounds (0, 0, 330, 300);
        addKnob (v, ids::bodyGain, "Body", 0, 0, true).setBounds (cell (0, 0, 1, 336).reduced (4));
        addKnob (v, ids::mudGain, "Mud", 1, 0, true).setBounds (cell (1, 0, 1, 336).reduced (4));
        addKnob (v, ids::mudFreq, "Mud freq", 2, 0).setBounds (cell (2, 0, 1, 336).reduced (4));
        addKnob (v, ids::presenceGain, "Presence", 0, 1, true).setBounds (cell (0, 1, 1, 336).reduced (4));
        addKnob (v, ids::presenceFreq, "Pres. freq", 1, 1).setBounds (cell (1, 1, 1, 336).reduced (4));
        addKnob (v, ids::airGain, "Air", 2, 1, true).setBounds (cell (2, 1, 1, 336).reduced (4));
    }

    // 5 De-esser (v1 Tame + auto band)
    {
        auto& v = detail.view (mDeess);
        addKnob (v, ids::tameFreq, "Freq", 0, 0);
        addKnob (v, ids::tameAmount, "Amount", 1, 0);
        addSwitch (v, ids::deessAuto, "Auto band (4-10 kHz)", { 230, 24, 240, 30 });
        deessInfo = makeReadout (v, "Split", { 230, 64, 400, 70 });
    }

    // 6 Compress: leveler, then the v1 Press
    {
        auto& v = detail.view (mCompress);
        addKnob (v, ids::levelAmount, "Leveler", 0, 0);
        addSwitch (v, ids::levelOn, "Leveler", { 8, 164, 110, 30 });
        addSwitch (v, ids::pressOn, "Press", { 8, 204, 110, 30 });
        addKnob (v, ids::pressThreshold, "Threshold", 1, 0).setBounds (cell (1, 0, 1, 24).reduced (4));
        addKnob (v, ids::pressRatio, "Ratio", 2, 0).setBounds (cell (2, 0, 1, 24).reduced (4));
        addKnob (v, ids::pressMakeup, "Makeup", 3, 0).setBounds (cell (3, 0, 1, 24).reduced (4));
        addKnob (v, ids::pressAttack, "Attack", 1, 1).setBounds (cell (1, 1, 1, 24).reduced (4));
        addKnob (v, ids::pressRelease, "Release", 2, 1).setBounds (cell (2, 1, 1, 24).reduced (4));
        parallelKnob = &addKnob (v, ids::pressParallel, "Parallel", 3, 1);
        parallelKnob->setBounds (cell (3, 1, 1, 24).reduced (4));
    }

    // 7 Saturate (v1 Heat)
    {
        auto& v = detail.view (mSaturate);
        addKnob (v, ids::heatDrive, "Drive", 0, 0);
        addKnob (v, ids::heatMix, "Mix", 1, 0);
    }

    // 8 Breath
    {
        auto& v = detail.view (mBreath);
        addKnob (v, ids::breathAmount, "Breaths", 0, 0);
    }

    // 9 Double + Width
    {
        auto& v = detail.view (mDouble);
        addKnob (v, ids::doubleAmount, "Double", 0, 0);
        addKnob (v, ids::width, "Width", 1, 0);
        addSwitch (v, ids::hookOnly, "Hook only (louder sections)", { 230, 24, 300, 30 });
    }

    // 10 Delay (v1 Space echo + ducking)
    {
        auto& v = detail.view (mDelay);
        addKnob (v, ids::delayTime, "Time", 0, 0, false, true);
        addKnob (v, ids::delayFeedback, "Feedback", 1, 0);
        addKnob (v, ids::delayMix, "Echo", 2, 0);
        addKnob (v, ids::echoDuck, "Duck", 3, 0);
        tempoInfo = makeReadout (v, "Tempo", { 440, 20, 200, 70 });
    }

    // 11 Reverb (v1 Space reverb + types, ducking)
    {
        auto& v = detail.view (mReverb);
        makeChoice (v, ids::verbType, { 4, 4, 420, 34 });
        auto shift = [] (juce::Rectangle<int> r) { return r.translated (0, 42).reduced (4).withHeight (126); };
        addKnob (v, ids::reverbSize, "Size", 0, 0).setBounds (shift (cell (0, 0)));
        addKnob (v, ids::reverbMix, "Verb", 1, 0).setBounds (shift (cell (1, 0)));
        addKnob (v, ids::verbDuck, "Duck", 2, 0).setBounds (shift (cell (2, 0)));
    }

    // 12 Output: v1 master + rider + limiter
    {
        auto& v = detail.view (mOutput);
        addKnob (v, ids::inputGain, "Input", 0, 0, true);
        addKnob (v, ids::outputGain, "Output", 1, 0, true);
        addKnob (v, ids::amount, "Amount", 2, 0);
        addKnob (v, ids::riderAmount, "Rider", 0, 1);
        addKnob (v, ids::limiterCeiling, "Ceiling", 1, 1);
        addSwitch (v, ids::riderOn, "Vocal rider", { 340, 16, 200, 30 });
        addSwitch (v, ids::limiterOn, "Safety limiter", { 340, 56, 200, 30 });
        addSwitch (v, ids::ceilingOn, "Soft ceiling (v1)", { 340, 96, 220, 30 });
        outputInfo = makeReadout (v, "Rider", { 340, 150, 290, 70 });
    }

    // The Read
    detail.view (DetailPanel::readView).addAndMakeVisible (readPanel);

    // OJU Beat
    {
        auto& v = detail.view (DetailPanel::beatView);
        beatInfo = makeReadout (v, "OJU Beat", { 0, 0, 640, 120 });
        addSwitch (v, ids::carveOn, "Carve (dip the beat under the vocal)", { 4, 140, 360, 30 });
        addKnob (v, ids::carveDepth, "Carve depth", 0, 0).setBounds (cell (4, 0).translated (0, 130).reduced (4).withHeight (140));
        makeChoice (v, ids::linkGroup, { 4, 186, 240, 34 });
    }

    readPanel.setBounds (detail.view (DetailPanel::readView).getLocalBounds());
    selectModule (DetailPanel::readView);
}

//==============================================================================
bool OjuEditor::moduleOn (int id) const
{
    auto on = [this] (const char* pid) { return ojuProcessor.getParameterValue (pid) > 0.5f; };
    switch (id)
    {
        case mCleanup:  return on (ids::lowCutOn) || on (ids::plosiveOn);
        case mCompress: return on (ids::levelOn) || on (ids::pressOn);
        case mDelay:    return on (ids::spaceOn) && on (ids::echoOn);
        case mReverb:   return on (ids::spaceOn) && on (ids::verbOn);
        case mOutput:   return on (ids::riderOn) || on (ids::limiterOn);
        default:        return on (moduleInfo (id).toggles[0].toRawUTF8());
    }
}

void OjuEditor::toggleModule (int id)
{
    const bool turnOn = ! moduleOn (id);
    for (const auto& t : moduleInfo (id).toggles)
        ojuProcessor.setParameterFromUI (t, turnOn ? 1.0f : 0.0f);
    if (turnOn && (id == mDelay || id == mReverb))
        ojuProcessor.setParameterFromUI (ids::spaceOn, 1.0f);
}

void OjuEditor::selectModule (int id)
{
    detail.show (id);
}

float OjuEditor::tileGr (int id) const
{
    switch (id)
    {
        case mDeess:    return meters.tame;
        case mCompress: return juce::jmax (meters.press, meters.level);
        case mOutput:   return meters.limiter;
        default:        return 0.0f;
    }
}

juce::String OjuEditor::tileStatus (int id) const
{
    auto val = [this] (const char* pid) { return ojuProcessor.getParameterValue (pid); };
    auto text = [this] (const char* pid)
    {
        if (auto* p = ojuProcessor.getState().getParameter (pid)) return p->getCurrentValueAsText();
        return juce::String();
    };
    const bool track = val (ids::latencyMode) > 0.5f;

    switch (id)
    {
        case mDenoise:
            if (track) return "Rests in Track mode";
            if (ojuProcessor.isLearningRoom()) return "Learning room...";
            return text (ids::denoiseAmount) + (ojuProcessor.hasRoomProfile() ? " + room" : juce::String());
        case mCleanup:  return "HPF " + text (ids::lowCutFreq) + (val (ids::plosiveOn) > 0.5f ? dot() + "pops tamed" : juce::String());
        case mTune:
            if (track) return "Rests in Track mode";
            return keyNames()[(int) val (ids::tuneKey)] + " " + scaleNames()[(int) val (ids::tuneScale)].toLowerCase();
        case mEq:       return "Presence " + text (ids::presenceGain);
        case mDeess:
        {
            const float f = val (ids::deessAuto) > 0.5f ? ojuProcessor.getChain().tameBandHz.load() : val (ids::tameFreq);
            return (val (ids::deessAuto) > 0.5f ? "Auto " : "") + hzText (f > 0.0f ? f : val (ids::tameFreq));
        }
        case mCompress: return (val (ids::levelOn) > 0.5f ? "Leveler + " : "") + text (ids::pressRatio);
        case mSaturate: return "Drive " + text (ids::heatDrive);
        case mBreath:   return "Breaths -" + juce::String (juce::roundToInt (val (ids::breathAmount) * 0.18f)) + " dB";
        case mDouble:   return val (ids::hookOnly) > 0.5f ? juce::String ("Hook only") : juce::String ("Whole song");
        case mDelay:    return text (ids::delayTime) + " @ " + juce::String (juce::roundToInt (ojuProcessor.getHostBpm()));
        case mReverb:   return verbTypeNames()[(int) val (ids::verbType)] + dot() + text (ids::reverbMix);
        case mOutput:   return "Ceiling " + text (ids::limiterCeiling);
        default:        return {};
    }
}

//==============================================================================
void OjuEditor::showPresetsMenu()
{
    juce::PopupMenu menu, genres, legacy, user;
    const auto styles = styleNames();
    for (int s : genreStyles())
        genres.addItem (100 + s, styles[s]);
    for (int s : { (int) Style::trap, (int) Style::pop, (int) Style::soul })
        legacy.addItem (100 + s, styles[s]);
    genres.addSubMenu ("OJU v1 styles", legacy);
    menu.addSubMenu ("Genre presets", genres);
    menu.addSeparator();
    menu.addItem (1, "Save preset...");

    const auto dir = OjuProcessor::presetsDirectory();
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.ojupreset");
    files.sort();
    for (int i = 0; i < files.size(); ++i)
        user.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    menu.addSubMenu ("My presets", user, ! files.isEmpty());
    menu.addItem (2, "Load preset from file...");
    menu.addItem (3, "Open presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton),
                        [this, files, dir] (int result)
    {
        if (result >= 1000 && result - 1000 < files.size())
        {
            ojuProcessor.loadPreset (files[result - 1000]);
        }
        else if (result >= 100)
        {
            ojuProcessor.setParameterFromUI (ids::style, (float) (result - 100));
        }
        else if (result == 1)
        {
            dir.createDirectory();
            chooser = std::make_unique<juce::FileChooser> ("Save OJU preset", dir.getChildFile ("My vocal.ojupreset"), "*.ojupreset");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                    ojuProcessor.savePreset (fc.getResult());
            });
        }
        else if (result == 2)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load OJU preset", dir, "*.ojupreset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile())
                    ojuProcessor.loadPreset (fc.getResult());
            });
        }
        else if (result == 3)
        {
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

//==============================================================================
// Standalone only: JUCE mutes the input by default to avoid feedback. Listening needs
// the mic, so we ask for a second tap (headphones on) before unmuting.
bool OjuEditor::standaloneInputMuted() const
{
   #if JucePlugin_Build_Standalone
    if (ojuProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return (bool) holder->getMuteInputValue().getValue();
   #endif
    return false;
}

void OjuEditor::unmuteStandaloneInput()
{
   #if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->getMuteInputValue().setValue (false);
   #endif
}

void OjuEditor::eyeClicked()
{
    const auto st = ojuProcessor.getListenState();
    const bool busy = st == Listener::State::listening || st == Listener::State::analysing;

    if (! busy && standaloneInputMuted())
    {
        if (unmuteArmedFrames <= 0)
        {
            unmuteArmedFrames = 30 * 8;
            timerCallback();
            return;
        }
        unmuteStandaloneInput();
    }

    unmuteArmedFrames = 0;
    ojuProcessor.toggleListening();
}

void OjuEditor::paint (juce::Graphics& g)
{
    g.fillAll (soot);
}

void OjuEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
}

//==============================================================================
void OjuEditor::timerCallback()
{
    auto& chain = ojuProcessor.getChain();
    auto& engine = ojuProcessor.getEngine();
    meters.in      = chain.inputPeak.exchange (0.0f);
    meters.out     = chain.outputPeak.exchange (0.0f);
    meters.press   = chain.pressGrDb.exchange (0.0f);
    meters.tame    = chain.tameGrDb.exchange (0.0f);
    meters.level   = chain.levelGrDb.exchange (0.0f);
    meters.limiter = engine.limiterGrDb.exchange (0.0f);
    meters.plosive = engine.plosiveDb.exchange (0.0f);

    inMeter.setValue (meters.in);
    grMeter.setValue (juce::jmax (meters.press, meters.level));
    outMeter.setValue (meters.out);

    const auto settings = ojuProcessor.readSettings();
    const int mode = settings.extreme ? 1 : 0;
    const auto styleName = styleNames()[(int) std::lround (ojuProcessor.getParameterValue (ids::style))];
    const auto modeName = modeNames()[mode];
    const bool beatRole = ojuProcessor.getParameterValue (ids::role) > 0.5f;

    // ---- AUTO eye
    const auto ls = ojuProcessor.getListenState();
    const float progress = ojuProcessor.getListenProgress();
    EyeButton::Phase phase = EyeButton::Phase::idle;
    switch (ls)
    {
        case Listener::State::listening: phase = EyeButton::Phase::listening; break;
        case Listener::State::analysing: phase = EyeButton::Phase::analysing; break;
        case Listener::State::done:      phase = EyeButton::Phase::done; break;
        case Listener::State::failed:    phase = EyeButton::Phase::failed; break;
        case Listener::State::idle:      break;
    }
    eye.update (phase, progress, meters.in);

    if (phase == EyeButton::Phase::listening)
    {
        stalledFrames = progress > lastProgress + 0.0001f ? 0 : stalledFrames + 1;
        lastProgress = progress;
        const int pct = juce::roundToInt (progress * 100.0f);
        if (stalledFrames > 60)
            eyeStatus.set ("Waiting for your voice", "Play the vocal or sing" + dot() + juce::String (pct) + "% heard");
        else
            eyeStatus.set ("Listening", juce::String (pct) + "%" + dot() + "keep singing" + dot() + "tap to stop");
    }
    else if (unmuteArmedFrames > 0)
    {
        --unmuteArmedFrames;
        eyeStatus.set ("Input is muted", "Headphones on, then tap again");
    }
    else
    {
        stalledFrames = 0;
        lastProgress = 0.0f;
        switch (phase)
        {
            case EyeButton::Phase::analysing: eyeStatus.set ("Reading the voice", "Setting all 12 modules"); break;
            case EyeButton::Phase::done:      eyeStatus.set ("Set for " + styleName, modeName + dot() + "tap to listen again"); break;
            case EyeButton::Phase::failed:    eyeStatus.set ("Didn't hear enough", "Tap the eye and sing again"); break;
            case EyeButton::Phase::idle:
            case EyeButton::Phase::listening:
                eyeStatus.set ("Auto", "Sing 10 s" + dot() + "OJU sets all 12 modules"); break;
        }
    }

    // ---- rack + detail
    for (int i = 0; i < numModules; ++i)
    {
        auto& t = *tiles[(size_t) i];
        t.setVisible (! beatRole);
        t.setState (moduleOn (i), detail.current() == i, tileStatus (i), tileGr (i));
    }
    if (beatRole && detail.current() != DetailPanel::beatView)
        selectModule (DetailPanel::beatView);
    else if (! beatRole && detail.current() == DetailPanel::beatView)
        selectModule (DetailPanel::readView);

    if (detail.current() < numModules)
        detail.setHeader (moduleOn (detail.current()), tileGr (detail.current()),
                          moduleInfo (detail.current()).fromV1 ? juce::String ("from OJU v1") : juce::String());

    const bool track = ojuProcessor.getParameterValue (ids::latencyMode) > 0.5f;
    if (denoiseInfo != nullptr)
    {
        juce::String t;
        if (track)                                 t = "Denoise rests in Track mode (it needs lookahead)";
        else if (ojuProcessor.isLearningRoom())    t = "Listening to the room... stay quiet (" + juce::String (juce::roundToInt (ojuProcessor.getRoomLearnProgress() * 100.0f)) + "%)";
        else if (ojuProcessor.hasRoomProfile())    t = "Room learned: its hum and hiss are removed too";
        else                                       t = "Not learned yet: press Learn room and stay quiet for 2 s";
        denoiseInfo->set (t);
    }
    if (cleanupInfo != nullptr)
    {
        const auto& f = ojuProcessor.getFeatures();
        cleanupInfo->set (f.valid ? "Lowest notes around " + hzText (f.lowestVoiceHz) : juce::String ("Press Auto to set the high-pass by voice"));
    }
    if (tuneInfo != nullptr)
        tuneInfo->set (track ? juce::String ("Tune rests in Track mode (it needs lookahead)")
                             : keyNames()[(int) ojuProcessor.getParameterValue (ids::tuneKey)] + " "
                               + scaleNames()[(int) ojuProcessor.getParameterValue (ids::tuneScale)].toLowerCase());
    if (deessInfo != nullptr)
    {
        const bool autoBand = ojuProcessor.getParameterValue (ids::deessAuto) > 0.5f;
        const float f = autoBand ? chain.tameBandHz.load() : ojuProcessor.getParameterValue (ids::tameFreq);
        deessInfo->set ((autoBand ? "Following the S's at " : "Split at ") + hzText (f > 0.0f ? f : ojuProcessor.getParameterValue (ids::tameFreq)));
    }
    if (tempoInfo != nullptr)
        tempoInfo->set (juce::String (juce::roundToInt (ojuProcessor.getHostBpm())) + " BPM from the host");
    if (outputInfo != nullptr)
    {
        const float rg = chain.riderGainDb.load();
        outputInfo->set (ojuProcessor.getParameterValue (ids::riderOn) > 0.5f
                             ? "Riding " + juce::String (rg >= 0.0f ? "+" : "") + juce::String (rg, 1) + " dB"
                             : juce::String ("Rider off"));
    }
    if (beatInfo != nullptr)
        beatInfo->set ("Put this on the beat track: it finds the key and sends it to the vocal OJU");

    if (parallelKnob != nullptr)
    {
        parallelKnob->setEnabled (mode == 1);
        parallelKnob->setNote (mode == 1 ? juce::String() : juce::String ("in Extreme"));
    }

    eqCurve.update (settings, ojuProcessor.getCurrentSampleRate(), ojuProcessor.getFeatures());
    readPanel.setContent (ojuProcessor.getReadLines(), ojuProcessor.getIdea(),
                          ojuProcessor.getReadLines().isEmpty() ? juce::String() : styleName + dot() + modeName, mode);
    if (ojuProcessor.getReadVersion() != lastReadVersion)
    {
        if (lastReadVersion >= 0 && ! beatRole)
            selectModule (DetailPanel::readView);
        lastReadVersion = ojuProcessor.getReadVersion();
    }

    // ---- status column
    const double sr = juce::jmax (1.0, ojuProcessor.getCurrentSampleRate());
    statusInfo.set ("CPU " + juce::String (ojuProcessor.getCpuLoad() * 100.0f, 1) + "% of a core",
                    "Latency " + juce::String (ojuProcessor.getLatencySamples() * 1000.0 / sr, 1) + " ms",
                    "No beat linked", false);

    abButton.setToggleState (ojuProcessor.getActiveSlot() == 1, juce::dontSendNotification);
    abButton.repaint();
    v1Button.setToggleState (ojuProcessor.isV1Compare(), juce::dontSendNotification);
}

//==============================================================================
void OjuEditor::EyeStatus::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawEngravedText (g, title, r.removeFromTop (38.0f), juce::Justification::centred, font (28.0f, true), brassLight);
    g.setFont (font (15.5f));
    g.setColour (boneDim);
    g.drawFittedText (subtitle, r.removeFromTop (44.0f).toNearestInt(), juce::Justification::centredTop, 2, 0.9f);
}

void OjuEditor::Readout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawRecess (g, r, 6.0f);
    auto inner = r.reduced (14.0f, 8.0f);
    drawEngravedText (g, name, inner.removeFromTop (20.0f), juce::Justification::centredLeft, font (14.0f, true), boneDim);
    g.setFont (font (17.5f, true));
    g.setColour (valueColour);
    g.drawFittedText (value, inner.toNearestInt(), juce::Justification::centredLeft, 3, 0.85f);
}

void OjuEditor::StatusInfo::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawRecess (g, r, 6.0f);
    auto inner = r.reduced (10.0f, 5.0f);
    auto row1 = inner.removeFromTop (18.0f);
    g.setFont (font (13.5f, true));
    g.setColour (brassLight);
    g.drawText (cpu, row1, juce::Justification::centredLeft);
    g.setFont (font (13.0f));
    g.setColour (boneDim);
    g.drawText (latency, inner.removeFromTop (16.0f), juce::Justification::centredLeft);

    auto row2 = inner;
    OjuLookAndFeel::drawLamp (g, row2.removeFromLeft (22.0f).withSizeKeepingCentre (20.0f, 20.0f), linked, false, 0.8f);
    row2.removeFromLeft (8.0f);
    g.setFont (font (13.5f, true));
    g.setColour (linked ? patina.brighter (0.4f) : boneDim);
    g.drawFittedText ("Beat Link" + juce::String::fromUTF8 ("\n") + link, row2.toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
}

} // namespace oju
