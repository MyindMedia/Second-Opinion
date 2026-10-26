#pragma once

// Averages frame chroma onto the beat grid (one feature vector per beat).

#include "AnalysisTypes.h"
#include "ChromaExtractor.h"

#include <vector>

namespace so::analysis
{

struct BeatFeatures
{
    Chroma chroma {};      // mean upper chroma over the beat
    Chroma bass {};        // mean bass chroma over the beat
    float energy { 0.0f }; // mean frame RMS
    bool silent { true };
};

/** One BeatFeatures per beat 0..numBeats-1. Frames whose centre lies inside the beat are averaged
    with a raised-sine weight (edges count 25%, the middle 100%) so the long analysis window
    smears less across chord changes. A beat is silent when its energy is below -80 dBFS or
    30 dB under the loudest beat (noise and release tails). */
std::vector<BeatFeatures> aggregateBeats (const ChromaFrames& frames, const BeatGrid& grid, int numBeats);

/** Divides by the maximum, subtracts `floor` and rescales so the peak is 1 again
    (all zeros stay zeros). */
Chroma normalizeChroma (const Chroma& c, float floor) noexcept;

} // namespace so::analysis
