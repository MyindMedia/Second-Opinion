#pragma once

// Template scoring over the full chord vocabulary x 12 roots x 12 bass notes, then Viterbi over
// beats with a beat-position-dependent change cost (harmonic rhythm).

#include "AnalysisTypes.h"
#include "BeatAggregator.h"

#include <atomic>
#include <optional>
#include <vector>

namespace so::analysis
{

struct ChordDecoderOptions
{
    int beatsPerBar { 4 };
    std::optional<theory::Key> key;   // diatonic prior and spelling; none = no prior, auto spelling

    float keyWeight { 0.15f };        // penalty per fraction of out-of-key chord tones
    float complexityWeight { 0.04f }; // per ChordQualityInfo::complexity step
    float temperature { 0.1f };       // score units per log-probability unit (Viterbi)
    float confidenceTemperature { 0.1f };

    // Cost (log units) of changing chord on: the downbeat, the middle strong beat, other beats.
    float changeCostDownbeat { 3.0f };
    float changeCostMidBar { 4.5f };
    float changeCostWeak { 8.0f };
};

/** One decoder state: root pitch class, quality and sounding bass pitch class. */
struct ChordState
{
    int root { 0 };
    theory::ChordQuality quality { theory::ChordQuality::Major };
    int bassPc { 0 };

    bool operator== (const ChordState&) const = default;
};

class ChordDecoder
{
public:
    static constexpr int numStates = theory::numChordQualities * 12 * 12;

    static int stateIndex (const ChordState& s) noexcept;
    static ChordState stateAt (int index) noexcept;

    /** Emission score (higher is better) of `s` for one beat. Silent beats score 0 everywhere. */
    static float score (const BeatFeatures& beat, const ChordState& s, const ChordDecoderOptions& options);

    /** Builds a spelled theory::Chord (root position leaves `bass` empty). Uses the key when given. */
    static theory::Chord toChord (const ChordState& s, const theory::Key* key);

    /** Decodes beats into merged segments. Leading silent beats are left uncovered; silent beats
        after the first sound extend the chord before them. Returns {} when cancelled or when
        every beat is silent. */
    std::vector<ChordSegment> decode (const std::vector<BeatFeatures>& beats, const ChordDecoderOptions& options,
                                      const std::atomic<bool>* cancel = nullptr) const;
};

} // namespace so::analysis
