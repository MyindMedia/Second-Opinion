#pragma once

// Frame-level pitch salience folded into 12-bin chroma (MIDI >= 52) and bass chroma (MIDI < 52).

#include "AnalysisTypes.h"

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace so::analysis
{

enum class AnalysisQuality { Fast, Balanced, Accurate };

inline constexpr int kLowestPitch = 24;    // C1
inline constexpr int kHighestPitch = 96;   // C7
inline constexpr int kBassSplit = 52;      // E3: below goes to bass chroma
inline constexpr int kNumPitches = kHighestPitch - kLowestPitch + 1;

using Chroma = std::array<float, 12>;

struct ChromaSettings
{
    int windowSize { 8192 };   // power of two
    int hopSize { 1024 };

    /** Fast: 4096 / 2048. Balanced: 8192 / 1024. Accurate: 8192 / 512 (all at 22,050 Hz). */
    static ChromaSettings forQuality (AnalysisQuality q) noexcept;
};

struct ChromaFrames
{
    double sampleRate { 22050.0 };
    ChromaSettings settings;
    std::vector<double> times;    // frame centre, seconds from the start of the buffer
    std::vector<Chroma> chroma;   // upper register (MIDI >= 52), log-compressed salience
    std::vector<Chroma> bass;     // MIDI < 52
    std::vector<float> energy;    // RMS of the frame

    size_t size() const noexcept { return times.size(); }
};

class ChromaExtractor
{
public:
    explicit ChromaExtractor (ChromaSettings s = {}) : settings (s) {}

    /** Analyses a mono buffer (normally 22,050 Hz). When `notes` is given, a note-derived chroma
        (velocity x confidence of the notes sounding at each frame) is blended in with
        `noteWeight` relative to the frame's own peak. Returns partial (possibly empty) frames
        when `cancel` becomes true. `progress` receives 0..1. */
    ChromaFrames process (const std::vector<float>& audio, double sampleRate,
                          const std::vector<NoteEvent>* notes = nullptr, float noteWeight = 0.5f,
                          const std::atomic<bool>* cancel = nullptr,
                          const std::function<void (float)>& progress = {}) const;

    /** Pitch salience (MIDI 24..96, linear amplitude) of one magnitude spectrum after peak
        picking and harmonic suppression. Exposed for tests. `mags` holds N/2 + 1 bins. */
    static std::array<float, kNumPitches> pitchSalience (const std::vector<float>& mags, int fftSize,
                                                         double sampleRate, float ampScale);

private:
    ChromaSettings settings;
};

} // namespace so::analysis
