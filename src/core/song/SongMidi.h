#pragma once

// Song -> MIDI lanes and drag-strip symbols (FR-SB-05). Uses each section's chosen option.

#include "../midi/SimpleVoicer.h"
#include "../theory/ChordNamer.h"
#include "SongBuilder.h"

#include <optional>
#include <string>
#include <vector>

namespace so::song
{

/** Chords of one section (beats from the section start) or the whole song (beats from bar 1). */
std::vector<midi::TimedChord> timedChords (const Song& song, std::optional<int> sectionIndex = std::nullopt);

/** Voiced Chords / Bass / Top line lanes, ready for midi::writeSmf and the drag strip. */
midi::VoicedLanes voiceSong (const Song& song);
midi::VoicedLanes voiceSection (const Song& song, int sectionIndex);

/** The lanes as a list in drag-strip order (Chords, Bass, Top line). */
std::vector<midi::MidiLane> toLanes (const midi::VoicedLanes& lanes);

struct ArrangementChord
{
    std::string symbol;   // ChordNamer::name in the given style (Pop by default)
    double beats { 4.0 };
};

/** The chosen arrangement as {symbol, beats} in order, for the existing drag strip
    (setProgression). Gaps are folded into the chord before them. */
std::vector<ArrangementChord> flattenArrangement (const Song& song, const theory::NamingOptions& naming = {},
                                                  std::optional<int> sectionIndex = std::nullopt);

} // namespace so::song
