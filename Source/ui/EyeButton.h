#pragma once

#include "Theme.h"

namespace oju
{
// The hero: a machined brass eye. Click to listen. Shows a progress ring while
// listening and its pupil breathes with the input level.
class EyeButton : public juce::Component
{
public:
    enum class Phase { idle, listening, analysing, done, failed };

    EyeButton();

    void update (Phase phase, float progress, float inputLevel);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hovered = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { pressed = true; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onClick;

private:
    Phase phase = Phase::idle;
    float progress = 0.0f, level = 0.0f, pupil = 0.0f, spin = 0.0f, doneFlash = 0.0f;
    bool hovered = false, pressed = false;
};

} // namespace oju
