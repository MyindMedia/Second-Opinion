#pragma once

// Shared data contract between analysis (producer), reharm and song builder (consumers), the
// bridge (JSON) and the UI (ui/src/state/types.ts). Change it only with all consumers in mind.

#include "../theory/Chord.h"
#include "../theory/Key.h"

#include <optional>
#include <string>
#include <vector>

namespace so::analysis
{

/** A transcribed note (Basic Pitch or the DSP melody tracker). Times in seconds from capture start. */
struct NoteEvent
{
    int pitch { 60 };            // MIDI
    double startSec { 0.0 };
    double endSec { 0.0 };
    float velocity { 0.8f };     // 0..1
    float confidence { 1.0f };   // 0..1
};

/** Tempo grid that maps beats to seconds. Beat 0 is bar 1, beat 1 of the capture. */
struct BeatGrid
{
    double bpm { 120.0 };
    int beatsPerBar { 4 };
    double downbeatSec { 0.0 };  // time of beat 0 in the audio

    double beatToSec (double beat) const noexcept { return downbeatSec + beat * 60.0 / bpm; }
    double secToBeat (double sec) const noexcept { return (sec - downbeatSec) * bpm / 60.0; }
};

struct ChordAlternative
{
    theory::Chord chord;
    float confidence { 0.0f };
};

/** One chord in the harmonic-rhythm grid (FR-AN-02). */
struct ChordSegment
{
    int id { 0 };                 // stable within one analysis
    double startBeat { 0.0 };
    double beats { 4.0 };
    theory::Chord chord;
    float confidence { 0.0f };    // posterior, 0..1
    std::vector<ChordAlternative> alternatives;   // top 3, best first, excluding `chord`
    bool userLocked { false };
    bool userCorrected { false };
};

/** A melody note on the beat grid (derived from NoteEvents). */
struct MelodyNote
{
    int pitch { 60 };
    double startBeat { 0.0 };
    double beats { 1.0 };
};

struct AnalysisResult
{
    int schemaVersion { 1 };
    BeatGrid grid;
    int bars { 0 };
    theory::Key key;
    float keyConfidence { 0.0f };
    float tuningCents { 0.0f };     // estimated global offset from A440, -50..50 (analysis only)
    std::vector<ChordSegment> segments;
    std::vector<MelodyNote> melody;
    std::vector<NoteEvent> notes;   // raw transcription, may be empty
};

/** Mono audio buffer handed to analysis (any sample rate; analysis resamples). */
struct AudioClip
{
    std::vector<float> samples;   // mono
    double sampleRate { 44100.0 };
};

} // namespace so::analysis
