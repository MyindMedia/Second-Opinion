#pragma once

// Basic Pitch (Spotify, Apache-2.0) note transcription on ONNX Runtime (P1-08, ADR 0009).
//
// A C++ port of basic_pitch/inference.py (windowing, overlap, stitching) and of
// basic_pitch/note_creation.py output_to_notes_polyphonic (onset/frame thresholds, minimum note
// length, melodia trick; pitch bends are skipped). No JUCE, no ONNX Runtime types in this header.
//
// Threading: run() blocks and must be called from a worker thread, never the audio thread.
// One runner may be used from one thread at a time; separate runners on separate threads are fine
// and share a single Ort::Env and one Ort::Session per model path (NFR-15).

#include "AnalysisTypes.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace so::analysis
{

/** Reference constants from basic_pitch/constants.py and inference.py. */
namespace basicpitch
{
    inline constexpr int kSampleRate = 22050;
    inline constexpr int kFftHop = 256;
    inline constexpr int kAudioSamples = kSampleRate * 2 - kFftHop;          // 43,844 per window
    inline constexpr int kFramesPerWindow = 172;                             // model output frames
    inline constexpr int kOverlapFrames = 30;
    inline constexpr int kOverlapSamples = kOverlapFrames * kFftHop;          // 7,680
    inline constexpr int kWindowHop = kAudioSamples - kOverlapSamples;       // 36,164
    inline constexpr int kNumPitches = 88;                                   // MIDI 21..108
    inline constexpr int kLowestMidi = 21;
} // namespace basicpitch

struct BasicPitchOptions
{
    double onsetThreshold { 0.5 };
    double frameThreshold { 0.3 };
    double minNoteLengthMs { 127.7 };   // 11 frames at 86.13 fps
    bool inferOnsets { true };
    bool melodiaTrick { true };
    int energyTolerance { 11 };
};

/** Frame-wise note posteriors ("frames" in Basic Pitch), the input for chroma (P2-01). */
struct NotePosteriorgram
{
    int numFrames { 0 };
    static constexpr int numPitches = basicpitch::kNumPitches;
    static constexpr int lowestMidi = basicpitch::kLowestMidi;
    std::vector<float> values;        // numFrames * numPitches, row-major (frame, pitch), 0..1
    std::vector<double> frameTimes;   // seconds, as basic_pitch model_frames_to_time()

    float at (int frame, int pitchIndex) const noexcept
    {
        return values[(std::size_t) frame * numPitches + (std::size_t) pitchIndex];
    }
};

struct BasicPitchResult
{
    bool ok { false };
    bool cancelled { false };
    std::string error;                // set when !ok && !cancelled
    std::vector<NoteEvent> notes;     // sorted by start time, then pitch
    NotePosteriorgram posteriorgram;
};

class BasicPitchRunner
{
public:
    struct Config
    {
        std::string modelPath;            // UTF-8 path to nmp.onnx
        // Optional UTF-8 path of the ONNX Runtime shared library to load explicitly (by full
        // path, so another plugin's copy is never picked up). Empty: on Windows the runner looks
        // for onnxruntime.dll next to the module that contains it, then falls back to the
        // delay-loaded import; on macOS and Linux the linked library (found through rpath) is used.
        std::string runtimeLibraryPath;
        int intraOpThreads { 2 };         // clamped to 1..2; first runner for a model wins
    };

    explicit BasicPitchRunner (Config config);
    ~BasicPitchRunner();

    BasicPitchRunner (const BasicPitchRunner&) = delete;
    BasicPitchRunner& operator= (const BasicPitchRunner&) = delete;

    /** Lazily creates the shared session. Safe to call repeatedly; returns false with lastError(). */
    bool prepare();
    const std::string& lastError() const noexcept { return error; }

    /** Transcribes a mono clip at any sample rate (resampled to 22,050 Hz internally).
        `cancel` is polled between model windows; `progress` (0..1) is updated after each. */
    BasicPitchResult run (const AudioClip& clip,
                          const BasicPitchOptions& options = {},
                          const std::atomic<bool>* cancel = nullptr,
                          std::atomic<float>* progress = nullptr);

    /** Same, for audio already at 22,050 Hz mono. */
    BasicPitchResult run22k (const float* samples, std::size_t numSamples,
                             const BasicPitchOptions& options = {},
                             const std::atomic<bool>* cancel = nullptr,
                             std::atomic<float>* progress = nullptr);

    /** Number of live shared sessions in this process (for tests and diagnostics). */
    static int sharedSessionCount();

    struct Shared;   // implementation detail (shared Ort::Env + Ort::Session)

private:
    Config config;
    std::shared_ptr<Shared> shared;
    std::string error;
};

/** Note creation only (output_to_notes_polyphonic + model_frames_to_time), on stitched
    posteriors with numFrames * 88 values each. Pure and deterministic. */
std::vector<NoteEvent> basicPitchNotesFromPosteriors (const float* notePosteriors,
                                                      const float* onsetPosteriors,
                                                      int numFrames,
                                                      const BasicPitchOptions& options = {});

/** basic_pitch model_frames_to_time() for frame indices 0..numFrames-1. */
std::vector<double> basicPitchFrameTimes (int numFrames);

namespace detail
{
    /** Small private Kaiser-windowed sinc resampler (deterministic). To be replaced by the
        shared analysis Resampler once it lands. */
    std::vector<float> resampleSinc (const float* input, std::size_t numSamples,
                                     double sourceRate, double targetRate);
} // namespace detail

} // namespace so::analysis
