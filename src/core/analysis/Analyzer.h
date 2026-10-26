#pragma once

// The listening engine: audio clip + beat grid -> key, chord segments and melody (PRD 7.4).

#include "AnalysisTypes.h"
#include "ChromaExtractor.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace so::analysis
{

enum class SourceHint { Auto, Vocal, Keys, Guitar, Loop };

/** Progress stages (FR-AN-01). "Writing suggestions" belongs to the reharm engine. */
enum class AnalysisStage : int { Preparing = 0, TranscribingNotes = 1, ReadingHarmony = 2 };

struct AnalyzerOptions
{
    AnalysisQuality quality { AnalysisQuality::Balanced };
    SourceHint source { SourceHint::Auto };
    std::optional<theory::Key> keyOverride;     // when set, key estimation is skipped
    int maxBars { 64 };
    std::optional<std::vector<NoteEvent>> notes; // e.g. Basic Pitch output; replaces the DSP melody tracker
};

/** stage is an AnalysisStage cast to int, pct runs 0..1 within the stage. */
using ProgressFn = std::function<void (int stage, float pct)>;

struct AnalysisStatus
{
    bool cancelled { false };
    std::string message;   // human-readable problem, empty when all went well
};

/** Runs the full pipeline. Deterministic: the same input always gives the same result.
    `cancel` is polled between stages and inside every long loop; on cancel the function returns
    early with whatever was finished and sets status->cancelled. */
AnalysisResult analyze (const AudioClip& clip, const BeatGrid& grid, const AnalyzerOptions& options = {},
                        ProgressFn progress = {}, const std::atomic<bool>* cancel = nullptr,
                        AnalysisStatus* status = nullptr);

} // namespace so::analysis
