#pragma once

// Hand-built AnalysisResult fixtures for the reharm tests.

#include "core/analysis/AnalysisTypes.h"
#include "core/theory/ChordSymbolParser.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace so::test
{

inline theory::Chord chord (const std::string& sym)
{
    auto c = theory::ChordSymbolParser::tryParse (sym);
    if (! c)
        throw std::runtime_error ("bad chord symbol in fixture: " + sym);
    return *c;
}

struct SegSpec { const char* symbol; double beats; };
struct NoteSpec { int pitch; double start; double beats; };

inline analysis::AnalysisResult build (const char* key, const std::vector<SegSpec>& segs,
                                       const std::vector<NoteSpec>& melody, int repeats = 1)
{
    analysis::AnalysisResult a;
    a.grid.bpm = 90.0;
    a.key = *theory::Key::parse (key);
    a.keyConfidence = 0.9f;

    double loopLen = 0.0;
    for (const auto& s : segs)
        loopLen += s.beats;

    int id = 1;
    for (int r = 0; r < repeats; ++r)
    {
        double t = r * loopLen;
        for (const auto& s : segs)
        {
            analysis::ChordSegment cs;
            cs.id = id++;
            cs.startBeat = t;
            cs.beats = s.beats;
            cs.chord = chord (s.symbol);
            cs.confidence = 0.9f;
            a.segments.push_back (cs);
            t += s.beats;
        }
        for (const auto& n : melody)
            a.melody.push_back ({ n.pitch, n.start + r * loopLen, n.beats });
    }
    a.bars = (int) (loopLen * repeats / 4.0);
    return a;
}

/** Fm9 | Dbmaj7 | Ab/C | Ebsus4 Eb | Fm9 | Dbmaj7 | Bbm7 | C7sus4 C7, with a singable melody. */
inline analysis::AnalysisResult fMinorLoop (int repeats = 1)
{
    return build ("F minor",
        { { "Fm9", 4 }, { "Dbmaj7", 4 }, { "Ab/C", 4 }, { "Ebsus4", 2 }, { "Eb", 2 },
          { "Fm9", 4 }, { "Dbmaj7", 4 }, { "Bbm7", 4 }, { "C7sus4", 2 }, { "C7", 2 } },
        { { 72, 0, 2 }, { 68, 2, 1 }, { 67, 3, 1 },
          { 65, 4, 2 }, { 68, 6, 2 },
          { 75, 8, 2 }, { 72, 10, 2 },
          { 70, 12, 2 }, { 67, 14, 2 },
          { 72, 16, 2 }, { 75, 18, 1 }, { 77, 19, 1 },
          { 77, 20, 2 }, { 72, 22, 2 },
          { 73, 24, 2 }, { 65, 26, 2 },
          { 65, 28, 2 }, { 64, 30, 2 } },
        repeats);
}

/** C | G/B | Am7 | Fmaj7 | C | Em7 | Dm7 G7 | C, a pop loop with a slash chord. */
inline analysis::AnalysisResult cMajorPop (int repeats = 1)
{
    return build ("C major",
        { { "C", 4 }, { "G/B", 4 }, { "Am7", 4 }, { "Fmaj7", 4 },
          { "C", 4 }, { "Em7", 4 }, { "Dm7", 2 }, { "G7", 2 }, { "C", 4 } },
        { { 76, 0, 2 }, { 74, 2, 1 }, { 72, 3, 1 },
          { 74, 4, 2 }, { 71, 6, 2 },
          { 72, 8, 2 }, { 76, 10, 2 },
          { 69, 12, 2 }, { 72, 14, 2 },
          { 67, 16, 2 }, { 64, 18, 2 },
          { 71, 20, 2 }, { 67, 22, 2 },
          { 65, 24, 2 }, { 71, 26, 2 },
          { 72, 28, 4 } },
        repeats);
}

} // namespace so::test
