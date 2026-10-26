#pragma once

#include <juce_graphics/juce_graphics.h>

/** Native mirror of the UI design tokens (ui/src/styles/tokens.css). Keep the two in sync. */
namespace so::theme
{
    inline const juce::Colour bg            { 0xff0e0f12 };
    inline const juce::Colour surface1      { 0xff15171c };
    inline const juce::Colour surface2      { 0xff1c1f26 };
    inline const juce::Colour surface3      { 0xff252934 };
    inline const juce::Colour border        { 0xff2e3340 };
    inline const juce::Colour borderStrong  { 0xff3d4352 };
    inline const juce::Colour text          { 0xffecedef };
    inline const juce::Colour text2         { 0xffa6acb8 };
    inline const juce::Colour text3         { 0xff6e7480 };
    inline const juce::Colour accent        { 0xffe3a44a };
    inline const juce::Colour accentHover   { 0xfff0b65e };
    inline const juce::Colour accentInk     { 0xff1a1307 };
    inline const juce::Colour original      { 0xff57c7b8 };

    inline constexpr float radiusSm = 4.0f;
    inline constexpr float radiusMd = 8.0f;
}
