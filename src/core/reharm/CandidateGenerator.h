#pragma once

// P4-01: per-segment replacement candidates for every enabled technique family, scored for
// melody compatibility and closeness to the Tension target.

#include "../analysis/AnalysisTypes.h"
#include "Settings.h"
#include "Suggestion.h"

#include <set>
#include <string>
#include <vector>

namespace so::reharm
{

/** One chord on the beat grid. */
struct Span
{
    double startBeat { 0.0 };
    double beats { 4.0 };
    theory::Chord chord;
};

/** A replacement for one segment. Its spans always cover exactly the segment's duration; an
    insertion keeps the original chord in its first span and adds the new chord(s) after it. */
struct Candidate
{
    int segmentIndex { 0 };          // index into the start-sorted segment list
    Technique technique { Technique::Original };
    std::string variant;             // e.g. "bVI", "V/ii", "richer"; groups plans by idea
    std::vector<Span> spans;
    int targetSegment { -1 };        // segment this candidate resolves into, or -1
    bool insertion { false };        // first span keeps the original chord
    double melodyFit { 1.0 };        // 0..1 over the changed spans
    double tension { 0.0 };          // duration-weighted chordTension over the changed spans
    double changedBeats { 0.0 };
    double functionFit { 0.0 };      // 0..1, how well it keeps the original's job
    double score { 0.0 };

    std::string groupKey() const;
};

class CandidateGenerator
{
public:
    /** Segments sorted by start beat (the order every other component uses). */
    static std::vector<analysis::ChordSegment> sortedSegments (const analysis::AnalysisResult& a);

    /** All candidates for all unlocked segments. Deterministic for the same inputs.
        `gateSlack` drops candidates whose tension exceeds the target by more than this. */
    static std::vector<Candidate> generate (const analysis::AnalysisResult& analysis, const Settings& settings,
                                            const std::set<int>& lockedSegmentIds, double gateSlack = 30.0);
};

} // namespace so::reharm
