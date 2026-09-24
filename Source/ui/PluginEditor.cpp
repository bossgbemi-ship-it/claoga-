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
    juce::String dash() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
}

OjuEditor::OjuEditor (OjuProcessor& p)
    : AudioProcessorEditor (p),
      ojuProcessor (p),
      styleChoice (p.getState(), ids::style),
      modeChoice (p.getState(), ids::mode)
{
    setLookAndFeel (&lookAndFeel);
    auto& state = p.getState();

    addAndMakeVisible (content);
    content.setBounds (0, 0, designWidth, designHeight);
    content.addAndMakeVisible (backplate);
    backplate.setBounds (content.getLocalBounds());

    // ---- top bar
    content.addAndMakeVisible (styleChoice);
    styleChoice.setBounds (layout::styleChoice);
    content.addAndMakeVisible (modeChoice);
    modeChoice.setBounds (layout::modeChoice);

    abButton.textProvider = [this] { return ojuProcessor.getActiveSlot() == 0 ? juce::String ("A  |  b") : juce::String ("a  |  B"); };
    abButton.onClick = [this] { ojuProcessor.toggleAB(); abButton.setToggleState (ojuProcessor.getActiveSlot() == 1, juce::dontSendNotification); abButton.repaint(); };
    abButton.setToggleState (ojuProcessor.getActiveSlot() == 1, juce::dontSendNotification);
    abButton.setTooltip ("Compare two settings");
    content.addAndMakeVisible (abButton);
    abButton.setBounds (layout::abButton);

    bypassButton.setClickingTogglesState (true);
    bypassButton.litColour = oxide.brighter (0.2f);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ids::bypass, bypassButton);
    content.addAndMakeVisible (bypassButton);
    bypassButton.setBounds (layout::bypassBtn);

    // ---- eye
    eye.onClick = [this] { eyeClicked(); };
    content.addAndMakeVisible (eye);
    eye.setBounds (layout::eye());
    content.addAndMakeVisible (eyeStatus);
    eyeStatus.setBounds (layout::eyeStatus());
    eyeStatus.setInterceptsMouseClicks (false, false);

    content.addAndMakeVisible (eqCurve);
    eqCurve.setBounds (layout::eqPanel.reduced (8));
    content.addAndMakeVisible (readPanel);
    readPanel.setBounds (layout::readPanel.reduced (8));

    // ---- meters
    {
        auto m = layout::meterPanel.reduced (22, 24);
        const int w = m.getWidth() / 3;
        for (auto* meter : { &inMeter, &grMeter, &outMeter })
        {
            content.addAndMakeVisible (*meter);
            meter->setBounds (m.removeFromLeft (w));
        }
    }

    // ---- Shape
    addKnob (ids::lowCutFreq, "Low cut", layout::shapeCard, 0, 0);
    addKnob (ids::bodyGain, "Body", layout::shapeCard, 1, 0, true);
    addKnob (ids::mudGain, "Mud", layout::shapeCard, 2, 0, true);
    addKnob (ids::mudFreq, "Mud freq", layout::shapeCard, 3, 0);
    addKnob (ids::presenceGain, "Presence", layout::shapeCard, 0, 1, true);
    addKnob (ids::presenceFreq, "Pres. freq", layout::shapeCard, 1, 1);
    addKnob (ids::airGain, "Air", layout::shapeCard, 2, 1, true);
    addSwitch (ids::lowCutOn, layout::cell (layout::shapeCard, 3, 1).withSizeKeepingCentre (84, 30), "Low cut");
    addSwitch (ids::eqOn, layout::cardSwitch (layout::shapeCard));

    // ---- Tame
    addKnob (ids::tameFreq, "Freq", layout::tameCard, 0, 0);
    addKnob (ids::tameAmount, "Amount", layout::tameCard, 0, 1);
    addSwitch (ids::tameOn, layout::cardSwitch (layout::tameCard));
    tameLamp.name = "tame";
    content.addAndMakeVisible (tameLamp);
    {
        const auto c = layout::cell (layout::tameCard, 0, 0);
        tameLamp.setBounds (c.getX() + 2, c.getY() + 2, 12, 12);
    }
    tameLamp.setInterceptsMouseClicks (false, false);

    // ---- Press
    addKnob (ids::pressThreshold, "Threshold", layout::pressCard, 0, 0);
    addKnob (ids::pressRatio, "Ratio", layout::pressCard, 1, 0);
    addKnob (ids::pressMakeup, "Makeup", layout::pressCard, 2, 0);
    addKnob (ids::pressAttack, "Attack", layout::pressCard, 0, 1);
    addKnob (ids::pressRelease, "Release", layout::pressCard, 1, 1);
    parallelKnob = &addKnob (ids::pressParallel, "Parallel", layout::pressCard, 2, 1);
    addSwitch (ids::pressOn, layout::cardSwitch (layout::pressCard));

    // ---- Heat
    addKnob (ids::heatDrive, "Drive", layout::heatCard, 0, 0);
    addKnob (ids::heatMix, "Mix", layout::heatCard, 0, 1);
    addSwitch (ids::heatOn, layout::cardSwitch (layout::heatCard));

    // ---- Space
    addKnob (ids::delayTime, "Time", layout::spaceCard, 0, 0, false, true);
    addKnob (ids::delayFeedback, "Feedback", layout::spaceCard, 1, 0);
    addKnob (ids::delayMix, "Echo", layout::spaceCard, 2, 0);
    addKnob (ids::reverbSize, "Size", layout::spaceCard, 0, 1);
    addKnob (ids::reverbMix, "Verb", layout::spaceCard, 1, 1);
    addSwitch (ids::spaceOn, layout::cardSwitch (layout::spaceCard));
    tempo.name = "Tempo";
    content.addAndMakeVisible (tempo);
    tempo.setBounds (layout::cell (layout::spaceCard, 2, 1));

    // ---- Master
    addKnob (ids::inputGain, "Input", layout::masterCard, 0, 0, true);
    addKnob (ids::outputGain, "Output", layout::masterCard, 1, 0, true);
    addKnob (ids::amount, "Amount", layout::masterCard, 0, 1);
    addSwitch (ids::ceilingOn, layout::cell (layout::masterCard, 1, 1).withSizeKeepingCentre (84, 30), "Ceiling");

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

Knob& OjuEditor::addKnob (const char* id, const juce::String& name, int card, int col, int row, bool bipolar, bool stepped)
{
    auto k = std::make_unique<Knob> (ojuProcessor.getState(), id, name, bipolar, stepped);
    content.addAndMakeVisible (*k);
    k->setBounds (layout::cell (card, col, row));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

LampSwitch& OjuEditor::addSwitch (const char* id, juce::Rectangle<int> bounds, const juce::String& label)
{
    auto s = std::make_unique<LampSwitch> (ojuProcessor.getState(), id, label);
    content.addAndMakeVisible (*s);
    s->setBounds (bounds);
    switches.push_back (std::move (s));
    return *switches.back();
}

//==============================================================================
// Standalone only: JUCE mutes the input by default to avoid feedback. Listening needs
// the mic, so we ask for a second tap (headphones on) before unmuting.
bool OjuEditor::standaloneInputMuted() const
{
   #if JucePlugin_Build_Standalone
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
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

void OjuEditor::timerCallback()
{
    auto& chain = ojuProcessor.getChain();
    const float inPk = chain.inputPeak.exchange (0.0f);
    inMeter.setValue (inPk);
    grMeter.setValue (chain.pressGrDb.exchange (0.0f));
    outMeter.setValue (chain.outputPeak.exchange (0.0f));
    tameLamp.set (chain.tameGrDb.exchange (0.0f) > 1.0f ? "on" : "");

    const auto settings = ojuProcessor.readSettings();
    const int mode = settings.extreme ? 1 : 0;
    const auto styleName = styleNames()[(int) std::lround (ojuProcessor.getState().getRawParameterValue (ids::style)->load())];
    const auto modeName = modeNames()[mode];

    // ---- eye
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
    eye.update (phase, progress, inPk);

    if (phase == EyeButton::Phase::listening)
    {
        stalledFrames = progress > lastProgress + 0.0001f ? 0 : stalledFrames + 1;
        lastProgress = progress;
        const int pct = juce::roundToInt (progress * 100.0f);
        if (stalledFrames > 60)
            eyeStatus.set ("Waiting for your voice", "Play the vocal or sing" + dash() + juce::String (pct) + "% heard" + dash() + "tap to stop");
        else
            eyeStatus.set ("Listening", juce::String (pct) + "%" + dash() + "keep singing" + dash() + "tap to stop");
    }
    else if (unmuteArmedFrames > 0)
    {
        --unmuteArmedFrames;
        eyeStatus.set ("Input is muted", "Headphones on, then tap the eye again to unmute");
    }
    else
    {
        stalledFrames = 0;
        lastProgress = 0.0f;
        const juce::String secs = mode == 1 ? "10" : "4";
        switch (phase)
        {
            case EyeButton::Phase::analysing: eyeStatus.set ("Reading the voice", "Setting up every module"); break;
            case EyeButton::Phase::done:      eyeStatus.set ("Set for " + styleName, modeName + dash() + "tap the eye to listen again"); break;
            case EyeButton::Phase::failed:    eyeStatus.set ("Didn't hear enough singing", "Tap the eye and try again"); break;
            case EyeButton::Phase::idle:
            case EyeButton::Phase::listening:
                eyeStatus.set ("Listen", "Sing a few lines" + dash() + modeName + " listens for " + secs + " seconds"); break;
        }
    }

    // ---- curve, read, extras
    eqCurve.update (settings, ojuProcessor.getCurrentSampleRate(), ojuProcessor.getFeatures());
    readPanel.setContent (ojuProcessor.getReadLines(), ojuProcessor.getIdea(),
                          ojuProcessor.getReadLines().isEmpty() ? juce::String() : styleName + dash() + modeName, mode);

    if (parallelKnob != nullptr)
    {
        parallelKnob->setEnabled (mode == 1);
        parallelKnob->setNote (mode == 1 ? juce::String() : juce::String ("in Extreme"));
    }

    tempo.set (juce::String (juce::roundToInt (ojuProcessor.getHostBpm())) + " BPM");
    abButton.setToggleState (ojuProcessor.getActiveSlot() == 1, juce::dontSendNotification);
}

//==============================================================================
void OjuEditor::EyeStatus::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawEngravedText (g, title, r.removeFromTop (40.0f), juce::Justification::centred, font (30.0f, true), brassLight);
    drawEngravedText (g, subtitle, r.removeFromTop (26.0f), juce::Justification::centred, font (16.0f), boneDim);
}

void OjuEditor::Readout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (name == "tame")
    {
        OjuLookAndFeel::drawLamp (g, r, value.isNotEmpty(), false, 0.6f);
        return;
    }
    auto well = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 8.0f, 70.0f), 48.0f).translated (0.0f, -10.0f);
    drawRecess (g, well, 5.0f);
    auto inner = well.reduced (2.0f, 5.0f);
    g.setFont (font (21.0f, true));
    g.setColour (brassLight);
    g.drawText (value.upToFirstOccurrenceOf (" ", false, false), inner.removeFromTop (inner.getHeight() * 0.62f), juce::Justification::centred, false);
    g.setFont (font (11.5f));
    g.setColour (boneDim);
    g.drawText (value.fromFirstOccurrenceOf (" ", false, false), inner, juce::Justification::centred, false);
    drawEngravedText (g, name, juce::Rectangle<float> (r.getX(), well.getBottom() + 6.0f, r.getWidth(), 17.0f),
                      juce::Justification::centred, font (14.5f), bone);
}

} // namespace oju
