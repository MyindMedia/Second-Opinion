#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace so::midi
{

struct MidiNote
{
    int pitch { 60 };
    double startBeat { 0.0 };
    double lengthBeats { 1.0 };
    int velocity { 96 };
    int channel { 0 };
};

struct MidiLane
{
    std::string name;
    std::vector<MidiNote> notes;
};

struct MidiFileSpec
{
    double bpm { 120.0 };
    int numerator { 4 };
    int denominator { 4 };
    int ppq { 960 };
};

/** Writes a Standard MIDI File, type 1: track 0 holds tempo and time signature, then one track per
    lane (FR-OUT-01 AC1). Pure C++, no JUCE, so the bytes are deterministic and golden-file testable.
    Notes start at beat 0 = bar 1 of the capture (AC2). */
std::vector<std::uint8_t> writeSmf (const std::vector<MidiLane>& lanes, const MidiFileSpec& spec);

} // namespace so::midi
