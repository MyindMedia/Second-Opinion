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

    /** Profile correlation plus tonal-centre evidence from the decoded chords, which is what
        separates relative major and minor (same notes, different home): the first and last chord,
        cadences into the tonic (V-i, bVII-i, IV-I, vii-i, including the loop wrap from the last
        chord back to the first), the share of time the tonic chord sounds and the share of time
        the bass sits on the tonic. Only the profile winner and its relative (when within 0.3 of
        it) get the evidence, added to their correlations before the softmax. With no segments
        this equals estimate (histogram (beats, &segments)). */
    static KeyEstimate estimateWithChords (const std::vector<BeatFeatures>& beats,
                                           const std::vector<ChordSegment>& segments);

    /** The tonal-centre bonus alone for one key (exposed for tests). */
    static float tonalCentreEvidence (const theory::Key& key, const std::vector<ChordSegment>& segments);

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
