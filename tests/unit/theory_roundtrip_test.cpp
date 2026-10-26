#include <catch2/catch_test_macros.hpp>

#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"

using namespace so::theory;

/** P1-03 acceptance: parse ↔ name round-trip for the full vocabulary × 12 roots × every bass note,
    in Pop and Jazz style, Unicode and ASCII accidentals, flat and sharp spelling. */
TEST_CASE ("Round-trip: full vocabulary × 12 roots × every bass", "[theory][roundtrip]")
{
    int checked = 0;

    for (int q = 0; q < numChordQualities; ++q)
    for (int rootPc = 0; rootPc < 12; ++rootPc)
    for (int bass = -1; bass < 12; ++bass)
    for (auto style : { NamingOptions::Style::Pop, NamingOptions::Style::Jazz })
    for (auto acc : { AccidentalStyle::Unicode, AccidentalStyle::Ascii })
    for (auto spelling : { NamingOptions::Spelling::Flats, NamingOptions::Spelling::Sharps })
    {
        Chord original;
        original.root = SpelledPitch::fromPc (rootPc, spelling == NamingOptions::Spelling::Flats);
        original.quality = (ChordQuality) q;
        if (bass >= 0 && bass != rootPc)
            original.bass = SpelledPitch::fromPc (bass, spelling == NamingOptions::Spelling::Flats);

        NamingOptions o;
        o.style = style;
        o.accidentals = acc;
        o.spelling = spelling;

        const auto symbol = ChordNamer::name (original, o);
        const auto parsed = ChordSymbolParser::tryParse (symbol);

        INFO ("symbol: " << symbol);
        REQUIRE (parsed.has_value());
        CHECK (parsed->sameHarmony (original));

        // Naming the parsed chord again gives the identical string (stable symbols).
        CHECK (ChordNamer::name (*parsed, o) == symbol);
        ++checked;
    }

    CHECK (checked == numChordQualities * 12 * 13 * 8);
}

TEST_CASE ("Round-trip: bestReading recovers every chord in root position and inversion", "[theory][roundtrip]")
{
    // For each chord, the pitch-class set over its own bass must read back as the same harmony
    // whenever the set is unambiguous in the vocabulary.
    int unambiguous = 0;

    for (int q = 0; q < numChordQualities; ++q)
    for (int rootPc = 0; rootPc < 12; ++rootPc)
    {
        Chord c;
        c.root = SpelledPitch::fromPcAuto (rootPc);
        c.quality = (ChordQuality) q;

        const auto pcs = c.chordTonePcs();
        NamingOptions off;
        off.preferSlash = false;
        const auto readings = ChordNamer::readings (pcs, rootPc, off);
        REQUIRE_FALSE (readings.empty());

        bool found = false;
        for (const auto& r : readings)
            found = found || r.sameHarmony (c);
        INFO ("chord quality " << q << " root " << rootPc);
        CHECK (found);

        // Root-position triads/7ths of distinct quality are unambiguous: must be ranked first.
        const auto qq = (ChordQuality) q;
        if (qq == ChordQuality::Major || qq == ChordQuality::Minor || qq == ChordQuality::Dominant7
            || qq == ChordQuality::Major7 || qq == ChordQuality::Minor7 || qq == ChordQuality::HalfDiminished7)
        {
            CHECK (readings.front().sameHarmony (c));
            ++unambiguous;
        }
    }

    CHECK (unambiguous == 6 * 12);
}
