#pragma once

// P4-02: turns per-segment candidates into whole-progression plans, scores them and picks a
// diverse set (MMR). The scoring sits behind IRanker so a learned model can replace it later.

#include "../analysis/AnalysisTypes.h"
#include "CandidateGenerator.h"
#include "Settings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace so::reharm
{

struct PlanFeatures
{
    double melodyFit { 1.0 };      // 0..1, changed chords only
    double tensionMatch { 0.0 };   // 0..1, closeness to the Tension target
    double voiceLeading { 0.0 };   // 0..1, chord-level smoothness relative to the original
    double novelty { 0.0 };        // 0..1, desirability of how much changed
    double tensionPush { 0.0 };    // -1..1, overall tension shift in the direction the knob asks for
};

class IRanker
{
public:
    virtual ~IRanker() = default;
    virtual double score (const PlanFeatures& f) const = 0;
};

class HeuristicRanker final : public IRanker
{
public:
    double score (const PlanFeatures& f) const override;
};

/** A coherent set of segment changes using one or two ideas. */
struct Plan
{
    std::vector<const Candidate*> picks;   // sorted by segment index, at most one per segment
    std::vector<std::string> groups;       // candidate group keys, primary first
    PlanFeatures features;
    double changedTension { 0.0 };
    double overallTension { 0.0 };          // predicted Suggestion::tension
    double score { 0.0 };                  // ranker score plus seeded jitter
    std::string key;                       // stable identity of the change set
};

class Ranker
{
public:
    /** Up to `count` plans, best first, deterministic for the same seed. */
    static std::vector<Plan> rank (const std::vector<Candidate>& candidates,
                                   const std::vector<analysis::ChordSegment>& segments,
                                   const theory::Key& key, const Settings& settings, int count, const IRanker& scorer);
};

} // namespace so::reharm
