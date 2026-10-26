#pragma once

// P3-01: exact dynamic programming over a pruned set of keyboard voicings per chord.

#include "../theory/Chord.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace so::reharm
{

struct VoicingRequest
{
    theory::Chord chord;
    int bassNote { -1 };              // sounding bass pitch (hard constraint: every voice sits above it)
    std::optional<int> topTarget;     // melody-on-top target pitch for the top voice
    bool locked { false };            // fixed constraint: isolated best voicing, independent of neighbors
};

struct SolverOptions
{
    int rangeLow { 36 };
    int rangeHigh { 84 };
    int density { 4 };
    int smoothness { 60 };            // 0..100
};

/** Cost weights. Only alpha follows Smoothness, which makes total movement provably
    non-increasing as Smoothness rises (exact DP over a fixed candidate set). */
struct SolverWeights
{
    double alpha { 1.0 };             // per semitone of total voice movement
    double beta { 5.0 };              // per parallel 5th / octave
    double gamma { 0.3 };             // per semitone the voicing's centre sits off the range centre
    double delta { 1.0 };             // spacing rules (seconds below C3, low-interval limits, mud)
    double epsilon { 4.0 };           // per semitone of melody-on-top miss (soft, fallback only)

    static SolverWeights fromSmoothness (int smoothness) noexcept;
};

struct Voicing
{
    std::vector<int> notes;           // sorted ascending MIDI notes, size == density
    double staticCost { 0.0 };
    std::uint64_t perfectPairs { 0 }; // bit (i*6+j): voices i<j form a P5 or P8 (mod 12)
};

class VoiceLeadingSolver
{
public:
    explicit VoiceLeadingSolver (SolverOptions options);

    /** One voicing per request, sorted ascending. Deterministic. */
    std::vector<std::vector<int>> solve (const std::vector<VoicingRequest>& requests);

    /** Pruned candidate voicings for one request, best static cost first (cached). */
    const std::vector<Voicing>& candidates (const VoicingRequest& r);

    /** Total semitone movement between two voicings of the same size (sorted voice pairing). */
    static int movement (const std::vector<int>& a, const std::vector<int>& b) noexcept;

    const SolverWeights& weights() const noexcept { return w; }

private:
    std::vector<Voicing> enumerate (const VoicingRequest& r, bool fixedTop) const;
    double transition (const Voicing& a, const Voicing& b, int bassA, int bassB) const;

    SolverOptions opt;
    SolverWeights w;
    std::map<std::string, std::vector<Voicing>> cache;
};

} // namespace so::reharm
