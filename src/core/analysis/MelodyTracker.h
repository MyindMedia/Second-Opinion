#pragma once

// Monophonic pitch tracking (YIN with FFT autocorrelation) and note segmentation.

#include "AnalysisTypes.h"

#include <atomic>
#include <functional>
#include <vector>

namespace so::analysis
{

struct MelodyTrackerOptions
{
    double minHz { 65.0 };           // C2
    double maxHz { 1500.0 };
    float yinThreshold { 0.15f };
    int windowSize { 1024 };         // YIN integration window (samples at the analysis rate)
    int hopSize { 256 };
    float silenceDb { -50.0f };      // absolute gate (dBFS RMS)
    float relativeGateDb { -30.0f }; // gate relative to the loudest frame
    double minNoteSec { 0.08 };
    float hysteresis { 0.2f };       // semitones beyond +-0.5 before a pitch change is accepted
    int changeFrames { 3 };          // consecutive frames a new pitch must hold
};

struct PitchFrame
{
    double timeSec { 0.0 };
    float midi { 0.0f };        // fractional MIDI, valid when voiced
    float confidence { 0.0f };  // 1 - aperiodicity
    float rms { 0.0f };
    bool voiced { false };
};

class MelodyTracker
{
public:
    explicit MelodyTracker (MelodyTrackerOptions o = {}) : options (o) {}

    /** Frame-wise pitch track. Returns partial output when cancelled. */
    std::vector<PitchFrame> pitchTrack (const std::vector<float>& audio, double sampleRate,
                                        const std::atomic<bool>* cancel = nullptr,
                                        const std::function<void (float)>& progress = {}) const;

    /** Pitch track segmented into notes (onset/offset refined on the sample envelope,
        notes shorter than minNoteSec dropped). */
    std::vector<NoteEvent> track (const std::vector<float>& audio, double sampleRate,
                                  const std::atomic<bool>* cancel = nullptr,
                                  const std::function<void (float)>& progress = {}) const;

private:
    MelodyTrackerOptions options;
};

/** The highest sounding voice of a polyphonic note list (for Basic Pitch input). A note is kept
    unless a higher note covers more than half of it. Sorted by start time. */
std::vector<NoteEvent> skyline (const std::vector<NoteEvent>& notes);

/** Places notes on the beat grid, quantised to `quantumBeats`, kept monophonic (a note is cut
    where the next begins), limited to [0, totalBeats). */
std::vector<MelodyNote> toMelodyNotes (const std::vector<NoteEvent>& notes, const BeatGrid& grid,
                                       double totalBeats, double quantumBeats = 0.25);

} // namespace so::analysis
