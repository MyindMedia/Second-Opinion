#pragma once

// Tempo and downbeat estimation for audio files dropped or opened without a host tempo
// (FR-IN-02 AC3 "detect"). Captures from the host keep using the host tempo and PPQ.

#include "AnalysisTypes.h"

#include <optional>

namespace so::analysis
{

struct TempoEstimate
{
    double bpm { 120.0 };
    double firstBeatSec { 0.0 };   // phase of the beat grid
    double downbeatSec { 0.0 };    // estimated bar start (strongest of the 4 beat phases)
    float confidence { 0.0f };     // 0..1, peak sharpness of the tempo autocorrelation
};

/** Spectral-flux onset envelope -> autocorrelation with harmonic summation over 60..180 BPM and a
    mild preference around 100 BPM -> best beat phase -> bar phase by low-frequency onset energy.
    Deterministic. Returns nullopt for silence or clips shorter than 4 seconds. */
std::optional<TempoEstimate> estimateTempo (const AudioClip& clip);

/** Picks the grid for a file: when the host tempo is known and the audio agrees with it (same
    tempo, half or double, within 4%), the host tempo wins; otherwise the detected tempo is used.
    Falls back to the host tempo, then 120 BPM. */
BeatGrid chooseFileGrid (const AudioClip& clip, std::optional<double> hostBpm);

/** Drops a final bar that is only a decaying tail: shorter than 60% of a bar, or quieter than 30%
    of the median bar level. Keeps at least one bar. Used for opened and dropped files. */
void trimTrailingTail (AudioClip& clip, const BeatGrid& grid);

} // namespace so::analysis
