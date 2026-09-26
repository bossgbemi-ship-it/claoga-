#pragma once

#include <juce_graphics/juce_graphics.h>

namespace oju::layout
{
// OJU 2.0: everything lives on a 1280 x 820 design canvas and is scaled as vectors.

// ---- top bar
inline const juce::Rectangle<int> topBar      { 0, 0, 1280, 72 };
inline const juce::Rectangle<int> logo        { 16, 8, 190, 56 };
inline const juce::Rectangle<int> roleChoice  { 216, 16, 110, 40 };
inline const juce::Rectangle<int> genreChoice { 336, 16, 450, 40 };
inline const juce::Rectangle<int> modeChoice  { 796, 16, 160, 40 };
inline const juce::Rectangle<int> presetsBtn  { 966, 16, 88, 40 };
inline const juce::Rectangle<int> abButton    { 1062, 16, 58, 40 };
inline const juce::Rectangle<int> v1Button    { 1128, 16, 50, 40 };
inline const juce::Rectangle<int> bypassBtn   { 1186, 16, 86, 40 };

// ---- middle
inline const juce::Rectangle<int> eyePanel    { 16, 84, 340, 388 };
inline const juce::Rectangle<int> detailPanel { 368, 84, 692, 388 };
inline const juce::Rectangle<int> statusPanel { 1072, 84, 192, 388 };

inline juce::Rectangle<int> eye()        { return { eyePanel.getX() + 45, eyePanel.getY() + 16, 250, 250 }; }
inline juce::Rectangle<int> eyeStatus()  { return { eyePanel.getX() + 12, eyePanel.getY() + 276, eyePanel.getWidth() - 24, 100 }; }

inline juce::Rectangle<int> meters()     { return { statusPanel.getX() + 16, statusPanel.getY() + 16, statusPanel.getWidth() - 32, 244 }; }
inline juce::Rectangle<int> latencyChoice() { return { statusPanel.getX() + 14, statusPanel.getY() + 268, statusPanel.getWidth() - 28, 34 }; }
inline juce::Rectangle<int> statusInfo() { return { statusPanel.getX() + 14, statusPanel.getY() + 306, statusPanel.getWidth() - 28, 72 }; }

// ---- the module rack: 12 tiles, chain order, two rows of six
inline const juce::Rectangle<int> rack        { 16, 484, 1248, 320 };
constexpr int rackCols = 6, rackGap = 10;

inline juce::Rectangle<int> tile (int index)
{
    const float w = (float) (rack.getWidth() - rackGap * (rackCols - 1)) / (float) rackCols;
    const float h = (float) (rack.getHeight() - rackGap) / 2.0f;
    const int col = index % rackCols, row = index / rackCols;
    return juce::Rectangle<float> ((float) rack.getX() + (float) col * (w + (float) rackGap),
                                   (float) rack.getY() + (float) row * (h + (float) rackGap), w, h).toNearestInt();
}

} // namespace oju::layout
