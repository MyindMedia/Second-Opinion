#pragma once

// Pure math for host capture (FR-IN-01 AC2, AC5): bar alignment, length limits and the mono
// downmix that turns a captured stereo buffer into the analysis contract types. Headless, no JUCE.

#include "core/analysis/AnalysisTypes.h"

#include <cstddef>

namespace so::audio
{

/** What the audio thread stamped when the first captured block arrived. PPQ is in quarter notes. */
struct CaptureStamp
{
    bool hasTempo { false };          // host reported a tempo
    bool hasPpq { false };            // host reported a PPQ position
    double bpm { 120.0 };             // quarter notes per minute
    double firstSamplePpq { 0.0 };    // PPQ of the first captured sample
    bool hasLastBarStart { false };
    double lastBarStartPpq { 0.0 };   // host's PPQ of the bar containing the first sample, if given
    int numerator { 4 };
    int denominator { 4 };
    double sampleRate { 48000.0 };
};

/** Bar length in quarter notes for a time signature (6/8 -> 3, 7/8 -> 3.5). */
double barLengthQuarters (int numerator, int denominator) noexcept;

/** Result of aligning a capture to the host's bar lines. */
struct BarAlignment
{
    analysis::BeatGrid grid;        // beats are quarter notes; beat 0 = first full bar in the audio
    double bar1Ppq { 0.0 };         // host PPQ of grid beat 0 (audition sync uses this)
};

/** Grid whose beat 0 is the first host bar line at or after the first captured sample (within
    `snapToleranceSamples`, so a capture that starts a hair after a bar line keeps that bar).
    Without tempo or PPQ (AC5) the grid uses `fallbackBpm` and the downbeat is the capture start. */
BarAlignment alignToBars (const CaptureStamp& stamp, double fallbackBpm = 120.0,
                          double snapToleranceSamples = 1.0) noexcept;

/** Capture length limit in frames: 64 bars at the stamp's tempo, capped at 3 minutes. Without tempo
    only the 3 minute cap applies. */
std::size_t captureLimitFrames (const CaptureStamp& stamp, int maxBars = 64, double maxSeconds = 180.0) noexcept;

/** Frames to pre-allocate for a capture at `bpm` (64 bars, capped at 3 minutes). */
std::size_t captureCapacityFrames (double sampleRate, double bpm, int numerator, int denominator,
                                   int maxBars = 64, double maxSeconds = 180.0) noexcept;

/** Averages `numChannels` planar channels of `numFrames` into a mono AudioClip. */
analysis::AudioClip downmixToMono (const float* const* channels, int numChannels, std::size_t numFrames,
                                   double sampleRate);

} // namespace so::audio
