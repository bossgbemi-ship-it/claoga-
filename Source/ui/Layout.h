#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cmath>

namespace oju::layout
{
// Everything lives on a 1280 x 820 design canvas and is scaled as vectors.
inline const juce::Rectangle<int> topBar      { 0, 0, 1280, 72 };
inline const juce::Rectangle<int> logo        { 24, 8, 230, 56 };
inline const juce::Rectangle<int> styleChoice { 270, 16, 520, 40 };
inline const juce::Rectangle<int> modeChoice  { 808, 16, 200, 40 };
inline const juce::Rectangle<int> abButton    { 1024, 16, 96, 40 };
inline const juce::Rectangle<int> bypassBtn   { 1132, 16, 124, 40 };

inline const juce::Rectangle<int> eyePanel    { 16, 86, 400, 450 };
inline const juce::Rectangle<int> eqPanel     { 428, 86, 648, 236 };
inline const juce::Rectangle<int> readPanel   { 428, 334, 648, 202 };
inline const juce::Rectangle<int> meterPanel  { 1088, 86, 176, 450 };

inline juce::Rectangle<int> eye()        { return { eyePanel.getX() + 45, eyePanel.getY() + 18, 310, 310 }; }
inline juce::Rectangle<int> eyeStatus()  { return { eyePanel.getX() + 16, eyePanel.getY() + 340, eyePanel.getWidth() - 32, 96 }; }

enum Card { shapeCard = 0, tameCard, pressCard, heatCard, spaceCard, masterCard, numCards };
inline constexpr std::array<float, numCards> cardColumns { 4.0f, 1.3f, 3.0f, 1.3f, 3.0f, 2.0f };
inline constexpr int cardsTop = 550, cardsHeight = 254, cardGap = 10, cardPad = 12, cardHeader = 36;

inline float columnWidth()
{
    float cols = 0.0f;
    for (auto c : cardColumns) cols += c;
    const int available = 1280 - 32 - cardGap * (numCards - 1) - cardPad * 2 * numCards;
    return (float) available / cols;
}

inline juce::Rectangle<int> card (int index)
{
    const float colW = columnWidth();
    float x = 16.0f;
    for (int i = 0; i < index; ++i)
        x += cardColumns[(size_t) i] * colW + (float) (cardPad * 2 + cardGap);
    const float w = cardColumns[(size_t) index] * colW + (float) (cardPad * 2);
    return juce::Rectangle<float> (x, (float) cardsTop, w, (float) cardsHeight).toNearestInt();
}

// Cell for a control inside a card (column, row in a 2-row grid).
inline juce::Rectangle<int> cell (int cardIndex, int col, int row)
{
    const auto c = card (cardIndex);
    const float colW = columnWidth() * cardColumns[(size_t) cardIndex] / std::floor (cardColumns[(size_t) cardIndex]);
    const float rowH = (float) (cardsHeight - cardHeader - 8) / 2.0f;
    return juce::Rectangle<float> ((float) (c.getX() + cardPad) + colW * (float) col,
                                   (float) (c.getY() + cardHeader) + rowH * (float) row,
                                   colW, rowH).toNearestInt().reduced (2, 2);
}

inline juce::Rectangle<int> cardSwitch (int cardIndex)
{
    const auto c = card (cardIndex);
    return { c.getRight() - 32, c.getY() + 7, 24, 24 };
}

} // namespace oju::layout
