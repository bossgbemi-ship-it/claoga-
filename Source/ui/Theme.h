#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace oju::theme
{
// Palette
inline const juce::Colour soot       { 0xff171513 };
inline const juce::Colour iron       { 0xff262320 };
inline const juce::Colour brass      { 0xffb8893f };
inline const juce::Colour oxide      { 0xff9c4a2f };
inline const juce::Colour bone       { 0xffe6dac2 };
inline const juce::Colour patina     { 0xff6e9486 };

// Derived tones
inline const juce::Colour ink        { 0xff0d0c0b };
inline const juce::Colour ironLight  { 0xff34302b };
inline const juce::Colour steel      { 0xff4b4741 };
inline const juce::Colour steelDark  { 0xff2d2a26 };
inline const juce::Colour brassLight { 0xffdcb86f };
inline const juce::Colour brassDark  { 0xff6f5020 };
inline const juce::Colour boneDim    { 0xff9d9484 };

// Design canvas (everything is laid out here, then scaled as vectors)
constexpr int designWidth = 1280;
constexpr int designHeight = 820;

// Typography: Bahnschrift (Windows), Avenir Next Condensed (macOS), sane fallbacks elsewhere
juce::Font font (float height, bool bold = false);

// Drawing helpers
void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Justification just, const juce::Font& f, juce::Colour colour, bool onLightMetal = false);
void drawRivet (juce::Graphics& g, juce::Point<float> centre, float radius);
void drawGrain (juce::Graphics& g, juce::Rectangle<float> area, int seed, float density, float alpha);
void drawBrushed (juce::Graphics& g, juce::Rectangle<float> area, int seed, float alpha);
void drawIronPlate (juce::Graphics& g, juce::Rectangle<float> area, float corner, int seed);
void drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float corner);
void drawBrassTrim (juce::Graphics& g, juce::Rectangle<float> area, float corner, float thickness);
juce::ColourGradient brassGradient (juce::Rectangle<float> area, float brightness = 1.0f);

} // namespace oju::theme
