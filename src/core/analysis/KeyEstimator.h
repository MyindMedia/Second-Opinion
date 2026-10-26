#pragma once

// Krumhansl-Kessler key-profile correlation over a duration-weighted pitch-class histogram.

#include "AnalysisTypes.h"
#include "BeatAggregator.h"

#include <array>
#include <vector>

namespace so::analysis
{

struct KeyEstimate
{
    theory::Key key;
    float confidence { 0.0f };               // softmax posterior of the best of 24 keys
    std::array<float, 24> correlation {};    // 0..11 major on C..B, 12..23 minor on C..B
};

class KeyEstimator
{
public:
    /** Correlates the histogram with the 24 rotated KK profiles. An all-zero histogram returns
        C major with confidence 0. */
    static KeyEstimate estimate (const std::array<double, 12>& histogram);

    /** Duration-weighted histogram: normalised chroma of every sounding beat (bass counts 50%)
        plus, when `segments` is given, the chord tones of each segment weighted by its length
        (root double, first and last segment 1.5x as tonal anchors). */
    static std::array<double, 12> histogram (const std::vector<BeatFeatures>& beats,
                                             const std::vector<ChordSegment>* segments = nullptr);

    /** Conventional tonic spelling for a pitch class and mode (fewest accidentals; F# major,
        E flat minor). */
    static theory::Key keyFor (int tonicPc, theory::Mode mode);
};

} // namespace so::analysis
