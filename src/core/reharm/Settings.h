#pragma once

// Reharmonization settings. Mirrors the plugin parameters (PRD 5.3, FR-RH-02..08).

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace so::reharm
{

/** Technique families the user can enable (FR-RH-03 Color). Values are bits of Settings::families. */
enum class Family : unsigned
{
    Diatonic         = 1u << 0,
    Borrowed         = 1u << 1,
    Secondary        = 1u << 2,
    TritoneSub       = 1u << 3,
    ChromaticMediant = 1u << 4,
    NeoRiemannian    = 1u << 5,
};

constexpr unsigned familyBit (Family f) noexcept { return static_cast<unsigned> (f); }
constexpr unsigned allFamilies = 0x3Fu;
constexpr Family allFamilyList[] { Family::Diatonic, Family::Borrowed, Family::Secondary,
                                   Family::TritoneSub, Family::ChromaticMediant, Family::NeoRiemannian };

/** "Diatonic", "Borrowed", "Secondary dominants", ... */
std::string_view toString (Family f) noexcept;

enum class BassMotion { Root, Inversions, Walking, Pedal };

/** FR-RH-07: the top voice equals the melody note, or sits a 3rd or 6th below it. */
enum class MelodyOnTop { Off, Exact, ThirdBelow, SixthBelow };

struct Settings
{
    int tension { 50 };                    // 0..100, FR-RH-02
    int smoothness { 60 };                 // 0..100, FR-RH-05
    unsigned families { allFamilies };     // bitmask of Family, FR-RH-03

    BassMotion bassMotion { BassMotion::Root };
    int pedalPc { -1 };                    // pitch class for Pedal; -1 = key tonic
    int pedalBars { 2 };                   // Pedal re-strikes every N bars

    int rangeLow { 36 };                   // chord lane, MIDI (FR-RH-06)
    int rangeHigh { 84 };
    int bassLow { 28 };                    // E1
    int bassHigh { 48 };                   // C3
    int density { 4 };                     // notes per chord voicing, 3..6

    MelodyOnTop melodyOnTop { MelodyOnTop::Off };
    std::uint64_t seed { 1 };              // Shuffle = new seed (FR-RH-01 AC3)
    int maxSuggestions { 12 };             // clamped to 6..12

    bool enabled (Family f) const noexcept { return (families & familyBit (f)) != 0; }

    /** Copy with every field clamped into its legal range. */
    Settings sanitized() const noexcept
    {
        Settings s = *this;
        s.tension = std::clamp (s.tension, 0, 100);
        s.smoothness = std::clamp (s.smoothness, 0, 100);
        s.families &= allFamilies;
        s.density = std::clamp (s.density, 3, 6);
        s.rangeLow = std::clamp (s.rangeLow, 0, 127);
        s.rangeHigh = std::clamp (s.rangeHigh, s.rangeLow + 12, 127);
        s.bassLow = std::clamp (s.bassLow, 0, 127);
        s.bassHigh = std::clamp (s.bassHigh, s.bassLow + 11, 127);
        s.pedalBars = std::clamp (s.pedalBars, 1, 64);
        s.maxSuggestions = std::clamp (s.maxSuggestions, 6, 12);
        return s;
    }
};

} // namespace so::reharm
