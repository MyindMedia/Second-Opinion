#pragma once

#include "Interval.h"

#include <array>
#include <string_view>
#include <vector>

namespace so::theory
{

/** The MVP chord vocabulary (PRD §7.4), plus m6 and 7sus4 which producers type constantly. */
enum class ChordQuality : int
{
    Major = 0,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Six,
    MinorSix,
    Dominant7,
    Major7,
    Minor7,
    HalfDiminished7,
    Diminished7,
    Add9,
    Dominant9,
    Major9,
    Minor9,
    Dominant7Sus4,
    Count
};

constexpr int numChordQualities = (int) ChordQuality::Count;

/** Chord family, used by Roman-numeral case and by the namer when breaking ties. */
enum class QualityFamily { Major, Minor, Diminished, Augmented, Suspended, Dominant };

struct ChordQualityInfo
{
    ChordQuality quality;
    std::vector<Interval> intervals;   // includes the root (0, degree 1)
    std::string_view pop;              // suffix in Pop style (ASCII accidentals; namer swaps glyphs)
    std::string_view jazz;             // suffix in Jazz style
    std::string_view roman;            // suffix after a Roman numeral (e.g. "7", "maj7", "ø7")
    QualityFamily family;
    int complexity;                    // lower = simpler; used to prefer plain names
};

/** Lookup table for every quality, indexed by (int) ChordQuality. */
const std::array<ChordQualityInfo, numChordQualities>& chordQualityTable();

inline const ChordQualityInfo& info (ChordQuality q) { return chordQualityTable()[(size_t) q]; }

/** Pitch-class bitmask (bit n = n semitones above the root). */
unsigned qualityMask (ChordQuality q);

} // namespace so::theory
