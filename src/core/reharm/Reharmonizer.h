#pragma once

// Entry point of the reharmonization engine (PRD 7.5). Headless, deterministic, no JUCE.

#include "../analysis/AnalysisTypes.h"
#include "Settings.h"
#include "Suggestion.h"

#include <set>
#include <vector>

namespace so::reharm
{

/** 6..12 whole-progression suggestions over the analysis. Locked segments (by ChordSegment::id,
    plus any segment with userLocked) come out byte-identical whatever the seed or the other
    settings that do not touch their own sound (tension, color, smoothness). Same inputs and seed
    give the same output; Shuffle = a new seed. */
std::vector<Suggestion> generate (const analysis::AnalysisResult& analysis, const Settings& settings,
                                  const std::set<int>& lockedSegmentIds);

/** Voices and explains an arbitrary progression of the analysis' segments unchanged
    (the "Original" card), with the same solver and bass settings. */
Suggestion voiceOriginal (const analysis::AnalysisResult& analysis, const Settings& settings,
                          const std::set<int>& lockedSegmentIds);

} // namespace so::reharm
