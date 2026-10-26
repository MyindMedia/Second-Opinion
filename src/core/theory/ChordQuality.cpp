#include "ChordQuality.h"

namespace so::theory
{

std::string Interval::label() const
{
    static constexpr int majorScaleSemis[] = { 0, 2, 4, 5, 7, 9, 11 };

    if (degree == 1)
        return "R";

    const int base = majorScaleSemis[(degree - 1) % 7] + (degree > 7 ? 12 : 0);
    int diff = semitones - base;

    // 7ths are measured against the major 7th, so a minor 7th reads "♭7" and dim7 reads "𝄫7".
    std::string prefix;
    if (diff == -1) prefix = "♭";
    else if (diff == -2) prefix = "𝄫";
    else if (diff == 1) prefix = "♯";

    return prefix + std::to_string (degree);
}

const std::array<ChordQualityInfo, numChordQualities>& chordQualityTable()
{
    using Q = ChordQuality;
    using F = QualityFamily;

    static const std::array<ChordQualityInfo, numChordQualities> table {{
        { Q::Major,           { {0,1}, {4,3}, {7,5} },                 "",      "",      "",     F::Major,      0 },
        { Q::Minor,           { {0,1}, {3,3}, {7,5} },                 "m",     "-",     "",     F::Minor,      0 },
        { Q::Diminished,      { {0,1}, {3,3}, {6,5} },                 "dim",   "°",     "°",    F::Diminished, 2 },
        { Q::Augmented,       { {0,1}, {4,3}, {8,5} },                 "aug",   "+",     "+",    F::Augmented,  2 },
        { Q::Sus2,            { {0,1}, {2,2}, {7,5} },                 "sus2",  "sus2",  "sus2", F::Suspended,  2 },
        { Q::Sus4,            { {0,1}, {5,4}, {7,5} },                 "sus4",  "sus4",  "sus4", F::Suspended,  1 },
        { Q::Six,             { {0,1}, {4,3}, {7,5}, {9,6} },          "6",     "6",     "6",    F::Major,      2 },
        { Q::MinorSix,        { {0,1}, {3,3}, {7,5}, {9,6} },          "m6",    "-6",    "6",    F::Minor,      3 },
        { Q::Dominant7,       { {0,1}, {4,3}, {7,5}, {10,7} },         "7",     "7",     "7",    F::Dominant,   1 },
        { Q::Major7,          { {0,1}, {4,3}, {7,5}, {11,7} },         "maj7",  "Δ7",    "maj7", F::Major,      1 },
        { Q::Minor7,          { {0,1}, {3,3}, {7,5}, {10,7} },         "m7",    "-7",    "7",    F::Minor,      1 },
        { Q::HalfDiminished7, { {0,1}, {3,3}, {6,5}, {10,7} },         "m7b5",  "ø7",    "ø7",   F::Diminished, 2 },
        { Q::Diminished7,     { {0,1}, {3,3}, {6,5}, {9,7} },          "dim7",  "°7",    "°7",   F::Diminished, 3 },
        { Q::Add9,            { {0,1}, {4,3}, {7,5}, {14,9} },         "add9",  "add9",  "add9", F::Major,      2 },
        { Q::Dominant9,       { {0,1}, {4,3}, {7,5}, {10,7}, {14,9} }, "9",     "9",     "9",    F::Dominant,   2 },
        { Q::Major9,          { {0,1}, {4,3}, {7,5}, {11,7}, {14,9} }, "maj9",  "Δ9",    "maj9", F::Major,      2 },
        { Q::Minor9,          { {0,1}, {3,3}, {7,5}, {10,7}, {14,9} }, "m9",    "-9",    "9",    F::Minor,      2 },
        { Q::Dominant7Sus4,   { {0,1}, {5,4}, {7,5}, {10,7} },         "7sus4", "7sus4", "7sus4",F::Suspended,  2 },
    }};

    return table;
}

unsigned qualityMask (ChordQuality q)
{
    unsigned m = 0;
    for (const auto& iv : info (q).intervals)
        m |= 1u << (iv.semitones % 12);
    return m;
}

} // namespace so::theory
