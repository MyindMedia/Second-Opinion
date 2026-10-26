#pragma once

// CPU-light preview synth for auditioning suggestions (FR-OUT-03, P3-04). Headless, no JUCE.
//
// Three voice types, 16-voice polyphony shared between them:
//   Keys: 2-operator FM electric piano (velocity drives brightness and level), ADSR, one-pole lowpass.
//   Pad:  three detuned polyBLEP saws, slow attack, two-pole lowpass.
//   Bass: sine plus a little saw, two-pole lowpass. Used for any lane whose name contains "bass".
//
// render() is real-time safe: no allocation, no locks, no exceptions. It plays an immutable
// PreviewSnapshot sample-accurately against a beat timeline supplied by the caller.

#include "core/midi/MidiWriter.h"

#include <array>
#include <cstdint>
#include <vector>

namespace so::preview
{

struct PreviewNote
{
    double startBeat { 0.0 };
    double endBeat { 1.0 };
    int pitch { 60 };
    float velocity { 0.75f };   // 0..1
    bool bass { false };
};

/** Immutable once built. Built on the message thread, read on the audio thread. */
struct PreviewSnapshot
{
    std::vector<PreviewNote> notes;   // sorted by startBeat
    double bpm { 120.0 };
    double lengthBeats { 16.0 };
};

/** Flattens MIDI lanes into a snapshot. Lanes named like "Bass" play the bass voice, the rest play
    the chord voice (keys or pad). Notes with non-positive length or outside MIDI range are dropped.
    A non-positive `lengthBeats` is replaced by the end of the last note (at least one beat). */
PreviewSnapshot makePreviewSnapshot (const std::vector<midi::MidiLane>& lanes, double bpm, double lengthBeats);

class PreviewSynth
{
public:
    enum class Sound { Keys = 0, Pad = 1, Off = 2 };
    static constexpr int maxVoices = 16;

    PreviewSynth();

    /** Not real-time safe in principle (called from prepareToPlay); does not allocate today. */
    void prepare (double sampleRate);

    /** Silences every voice immediately and forgets the playback position. */
    void reset() noexcept;

    /** Releases sounding voices; fast = a ~15 ms fade instead of the voice's own release. */
    void releaseAll (bool fast) noexcept;

    /** Forces the next render() to re-seek (retrigger notes sounding at that position). */
    void resync() noexcept { expectedBeat = -1.0e300; }

    void setSound (Sound s) noexcept { sound = s; }
    Sound getSound() const noexcept { return sound; }

    /** Adds `numSamples` of audio to left and right (two distinct buffers). The timeline is
        `startBeat + i * beatsPerSample`. A jump in the timeline (seek, loop wrap) or a new snapshot
        pointer fast-releases held notes and restarts from the new position, including notes that
        are already sounding at that position. `snapshot` may be null (only tails are rendered). */
    void render (const PreviewSnapshot* snapshot, float* left, float* right, int numSamples,
                 double startBeat, double beatsPerSample) noexcept;

    int activeVoiceCount() const noexcept;

private:
    enum class Kind : std::uint8_t { Keys, Pad, Bass };
    enum class Stage : std::uint8_t { Off, Attack, Decay, Sustain, Release };

    struct EnvParams
    {
        float attackInc { 0.0f };    // linear attack, per sample
        float decayCoef { 0.0f };    // exponential, per sample
        float sustain { 0.0f };
        float releaseCoef { 0.0f };
    };

    struct Voice
    {
        Stage stage { Stage::Off };
        Kind kind { Kind::Keys };
        int pitch { 0 };
        double endBeat { 0.0 };
        std::uint64_t age { 0 };
        float gain { 0.0f };
        float env { 0.0f };
        float releaseCoef { 0.0f };
        float phase[3] { 0.0f, 0.0f, 0.0f };
        float inc[3] { 0.0f, 0.0f, 0.0f };
        float modPhase { 0.0f };
        float modInc { 0.0f };
        float modIndex { 0.0f };
        float modDecay { 1.0f };
        float lpG { 0.5f };
        float lp1 { 0.0f };
        float lp2 { 0.0f };
        bool held() const noexcept { return stage != Stage::Off && stage != Stage::Release; }
    };

    void seek (const PreviewSnapshot& snap, double beat) noexcept;
    void startNote (const PreviewNote& n) noexcept;
    void renderVoices (float* left, float* right, int numSamples) noexcept;
    float sine (float phase) const noexcept;
    const EnvParams& envFor (Kind k) const noexcept;
    float onePoleG (float cutoffHz) const noexcept;

    std::array<Voice, maxVoices> voices {};
    double sampleRate { 48000.0 };
    EnvParams keysEnv, padEnv, bassEnv;
    float fastReleaseCoef { 0.99f };
    float keysModDecay { 0.9999f };
    float bassLpG { 0.1f };
    float padLpG { 0.2f };

    Sound sound { Sound::Keys };
    const PreviewSnapshot* lastSnapshot { nullptr };
    double expectedBeat { -1.0e300 };
    std::size_t cursor { 0 };
    std::uint64_t ageCounter { 0 };
};

} // namespace so::preview
