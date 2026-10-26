#include <catch2/catch_test_macros.hpp>

#include "core/midi/MidiWriter.h"

#include <algorithm>
#include "core/midi/SimpleVoicer.h"
#include "core/theory/ChordSymbolParser.h"

using namespace so::midi;

TEST_CASE ("SMF header: type 1, 960 PPQ, conductor + lanes", "[midi]")
{
    MidiLane lane { "Chords", { { 60, 0.0, 4.0, 90, 0 } } };
    const auto bytes = writeSmf ({ lane }, { 92.0, 4, 4, 960 });

    REQUIRE (bytes.size() > 22);
    CHECK (std::string (bytes.begin(), bytes.begin() + 4) == "MThd");
    CHECK (bytes[9] == 1);           // format 1
    CHECK (bytes[11] == 2);          // conductor + 1 lane
    CHECK (bytes[12] == 0x03);       // 960 = 0x03C0
    CHECK (bytes[13] == 0xC0);
}

TEST_CASE ("Tempo meta event encodes BPM", "[midi]")
{
    const auto bytes = writeSmf ({}, { 120.0, 4, 4, 960 });
    // 120 BPM = 500000 µs per quarter = 0x07A120
    const std::vector<std::uint8_t> tempo { 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20 };
    CHECK (std::search (bytes.begin(), bytes.end(), tempo.begin(), tempo.end()) != bytes.end());
}

TEST_CASE ("Golden bytes for a single C4 whole note", "[midi][golden]")
{
    MidiLane lane { "L", { { 60, 0.0, 4.0, 100, 0 } } };
    const auto bytes = writeSmf ({ lane }, { 120.0, 4, 4, 960 });

    // Lane track: name "L", note on at 0, note off at 3840 ticks (0x9E 0x00 as var-len), end of track.
    const std::vector<std::uint8_t> expectedTrack {
        'M','T','r','k', 0,0,0,0x12,
        0x00, 0xff, 0x03, 0x01, 'L',
        0x00, 0x90, 60, 100,
        0x9e, 0x00, 0x80, 60, 0,
        0x00, 0xff, 0x2f, 0x00 };

    REQUIRE (bytes.size() >= expectedTrack.size());
    CHECK (std::vector<std::uint8_t> (bytes.end() - (long) expectedTrack.size(), bytes.end()) == expectedTrack);
}

TEST_CASE ("Writer and voicer are deterministic", "[midi][determinism]")
{
    using so::theory::ChordSymbolParser;
    std::vector<TimedChord> prog;
    double beat = 0;
    for (auto s : { "Fm9", "Dbmaj7", "Ab/C", "Ebsus4" })
    {
        prog.push_back ({ *ChordSymbolParser::tryParse (s), beat, 4.0 });
        beat += 4.0;
    }

    const auto a = voiceProgression (prog);
    const auto b = voiceProgression (prog);
    const MidiFileSpec spec { 92.0, 4, 4, 960 };
    CHECK (writeSmf ({ a.chords, a.bass, a.top }, spec) == writeSmf ({ b.chords, b.bass, b.top }, spec));
}

TEST_CASE ("Voicer keeps notes in range and keeps slash basses", "[midi][voicer]")
{
    using so::theory::ChordSymbolParser;
    std::vector<TimedChord> prog;
    double beat = 0;
    for (auto s : { "G/B", "C", "G/F", "C/E", "Fmaj9", "Bm7b5", "E7", "Am" })
    {
        prog.push_back ({ *ChordSymbolParser::tryParse (s), beat, 2.0 });
        beat += 2.0;
    }

    const auto v = voiceProgression (prog);
    REQUIRE (v.bass.notes.size() == prog.size());

    for (size_t i = 0; i < prog.size(); ++i)
        CHECK (v.bass.notes[i].pitch % 12 == prog[i].chord.bassPc());

    for (const auto& n : v.bass.notes)   { CHECK (n.pitch >= 28); CHECK (n.pitch <= 48); }
    for (const auto& n : v.chords.notes) { CHECK (n.pitch >= 48); CHECK (n.pitch <= 84); }
    CHECK (v.top.notes.size() == prog.size());
}
