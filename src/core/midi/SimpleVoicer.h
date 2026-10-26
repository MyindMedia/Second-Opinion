#pragma once

#include "../theory/Chord.h"
#include "MidiWriter.h"

#include <vector>

namespace so::midi
{

struct TimedChord
{
    theory::Chord chord;
    double startBeat { 0.0 };
    double lengthBeats { 4.0 };
};

struct VoicedLanes
{
    MidiLane chords { "Chords", {} };
    MidiLane bass { "Bass", {} };
    MidiLane top { "Top line", {} };
};

/** Placeholder voicer used until the real VoiceLeadingSolver lands (P3-01).
    Upper voices: chord tones in close position inside C3..C6, choosing for each chord the inversion
    with the least total movement from the previous one. Bass: the written bass (or root) in E1..C3,
    nearest to the previous bass note. Slash basses are always kept. Deterministic. */
VoicedLanes voiceProgression (const std::vector<TimedChord>& progression);

} // namespace so::midi
