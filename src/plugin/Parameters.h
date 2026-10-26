#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace so::params
{

// Parameter IDs. These are part of saved projects and host automation: never rename them.
inline constexpr auto tension      = "tension";
inline constexpr auto smoothness   = "smoothness";
inline constexpr auto bassMotion   = "bassMotion";
inline constexpr auto density      = "density";
inline constexpr auto rangeLow     = "rangeLow";
inline constexpr auto rangeHigh    = "rangeHigh";
inline constexpr auto melodyOnTop  = "melodyOnTop";
inline constexpr auto previewLevel = "previewLevel";

// Color technique families (FR-RH-03), one automatable toggle each.
inline constexpr std::array<const char*, 6> colorIds {
    "colorDiatonic", "colorBorrowed", "colorSecondary", "colorTritone", "colorMediant", "colorNeoRiemann"
};
inline constexpr std::array<const char*, 6> colorNames {
    "Diatonic", "Borrowed", "Secondary dominants", "Tritone subs", "Chromatic mediants", "Neo-Riemannian"
};

inline const juce::StringArray bassMotionChoices { "Root", "Inversions", "Walking", "Pedal" };

constexpr int parameterVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace so::params
