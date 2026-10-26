#include <catch2/catch_test_macros.hpp>

#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"
#include "core/theory/Key.h"

using namespace so::theory;

namespace
{
    Chord parse (std::string_view s)
    {
        auto c = ChordSymbolParser::tryParse (s);
        REQUIRE (c.has_value());
        return *c;
    }

    std::string pop (const Chord& c) { return ChordNamer::name (c); }

    std::string jazz (const Chord& c)
    {
        NamingOptions o;
        o.style = NamingOptions::Style::Jazz;
        return ChordNamer::name (c, o);
    }
}

TEST_CASE ("Parser accepts the examples from the PRD", "[theory][parser]")
{
    CHECK (pop (parse ("G/B")) == "G/B");
    CHECK (pop (parse ("Am7")) == "Am7");
    CHECK (pop (parse ("F#m7b5")) == "F♯m7♭5");
    CHECK (pop (parse ("Csus2")) == "Csus2");
    CHECK (pop (parse ("Dbmaj7/F")) == "D♭maj7/F");
    CHECK (pop (parse ("D♭maj7/F")) == "D♭maj7/F");
    CHECK (pop (parse ("Fm9")) == "Fm9");
    CHECK (pop (parse ("Ab/C")) == "A♭/C");
    CHECK (pop (parse ("Ebsus4")) == "E♭sus4");
}

TEST_CASE ("Parser understands Jazz and shorthand spellings", "[theory][parser]")
{
    CHECK (parse ("C-7").quality == ChordQuality::Minor7);
    CHECK (parse ("CΔ7").quality == ChordQuality::Major7);
    CHECK (parse ("CΔ").quality == ChordQuality::Major7);
    CHECK (parse ("CM7").quality == ChordQuality::Major7);
    CHECK (parse ("Cø7").quality == ChordQuality::HalfDiminished7);
    CHECK (parse ("Cø").quality == ChordQuality::HalfDiminished7);
    CHECK (parse ("C°7").quality == ChordQuality::Diminished7);
    CHECK (parse ("Co7").quality == ChordQuality::Diminished7);
    CHECK (parse ("C°").quality == ChordQuality::Diminished);
    CHECK (parse ("C+").quality == ChordQuality::Augmented);
    CHECK (parse ("Csus").quality == ChordQuality::Sus4);
    CHECK (parse ("G7sus").quality == ChordQuality::Dominant7Sus4);
    CHECK (parse ("Cadd2").quality == ChordQuality::Add9);
    CHECK (parse ("C-6").quality == ChordQuality::MinorSix);
    CHECK (parse ("Cmin").quality == ChordQuality::Minor);
    CHECK (parse ("  G / B ").bass->pc() == 11);
}

TEST_CASE ("Parser gives human errors", "[theory][parser]")
{
    auto err = [] (std::string_view s)
    {
        auto r = ChordSymbolParser::parse (s);
        REQUIRE (std::holds_alternative<ParseError> (r));
        return std::get<ParseError> (r).message;
    };

    CHECK (err ("").find ("Type a chord") != std::string::npos);
    CHECK (err ("H7").find ("note name") != std::string::npos);
    CHECK (err ("Cxyz").find ("Unknown chord type") != std::string::npos);
    CHECK (err ("G/").find ("bass note") != std::string::npos);
    CHECK (err ("G/X").find ("bass") != std::string::npos);
}

TEST_CASE ("C/C collapses to C", "[theory][parser]")
{
    auto c = parse ("C/C");
    CHECK_FALSE (c.bass.has_value());
    CHECK (pop (c) == "C");
}

TEST_CASE ("Jazz style names", "[theory][namer]")
{
    CHECK (jazz (parse ("Cmaj7")) == "CΔ7");
    CHECK (jazz (parse ("Cm7")) == "C-7");
    CHECK (jazz (parse ("Bm7b5")) == "Bø7");
    CHECK (jazz (parse ("Bdim7")) == "B°7");
    CHECK (jazz (parse ("Ebmaj9/G")) == "E♭Δ9/G");
}

TEST_CASE ("Spelling preference respells root and bass", "[theory][namer]")
{
    NamingOptions sharps;
    sharps.spelling = NamingOptions::Spelling::Sharps;
    NamingOptions flats;
    flats.spelling = NamingOptions::Spelling::Flats;
    NamingOptions ascii;
    ascii.accidentals = AccidentalStyle::Ascii;

    CHECK (ChordNamer::name (parse ("Dbmaj7/F"), sharps) == "C♯maj7/E♯");
    CHECK (ChordNamer::name (parse ("C#m7"), flats) == "D♭m7");
    CHECK (ChordNamer::name (parse ("F#m7b5"), ascii) == "F#m7b5");
    CHECK (ChordNamer::name (parse ("Bbm7b5"), ascii) == "Bbm7b5");
}

TEST_CASE ("G/F is named G/F, never F6sus2b5", "[theory][namer][slash]")
{
    // The forum complaint (PRD §3): upper G-B-D over F must read as G/F.
    const auto best = ChordNamer::bestReading ({ 7, 11, 2 }, 5);
    REQUIRE (best.has_value());
    CHECK (pop (*best) == "G/F");

    // Even if the F is doubled above, the slash reading wins with "prefer slash" on.
    const auto doubled = ChordNamer::bestReading ({ 7, 11, 2, 5 }, 5);
    REQUIRE (doubled.has_value());
    CHECK (pop (*doubled) == "G7/F");
}

TEST_CASE ("G/B keeps its inversion", "[theory][namer][slash]")
{
    const auto best = ChordNamer::bestReading ({ 7, 2, 11 }, 11);
    REQUIRE (best.has_value());
    CHECK (pop (*best) == "G/B");
    CHECK (best->bassIsChordTone());
}

TEST_CASE ("Prefer slash toggles C/A vs Am7", "[theory][namer][slash]")
{
    NamingOptions slashOn;
    NamingOptions slashOff;
    slashOff.preferSlash = false;

    CHECK (pop (*ChordNamer::bestReading ({ 0, 4, 7 }, 9, slashOn)) == "C/A");
    CHECK (pop (*ChordNamer::bestReading ({ 0, 4, 7 }, 9, slashOff)) == "Am7");

    CHECK (pop (*ChordNamer::bestReading ({ 4, 7, 11 }, 0, slashOn)) == "Em/C");
    CHECK (pop (*ChordNamer::bestReading ({ 4, 7, 11 }, 0, slashOff)) == "Cmaj7");
}

TEST_CASE ("Root-position readings when the bass is not repeated", "[theory][namer]")
{
    CHECK (pop (*ChordNamer::bestReading ({ 11, 2 }, 7)) == "G");
    CHECK (pop (*ChordNamer::bestReading ({ 0, 4, 7, 9 }, 9)) == "Am7");
    CHECK (pop (*ChordNamer::bestReading ({ 0, 4, 7, 9 }, 0)) == "C6");
    CHECK (pop (*ChordNamer::bestReading ({ 0, 3, 7, 10 }, 0)) == "Cm7");
}

TEST_CASE ("Key-aware spelling", "[theory][namer]")
{
    const auto fMinor = *Key::parse ("F minor");
    CHECK (pop (*ChordNamer::bestReading ({ 1, 5, 8, 0 }, 1, {}, &fMinor)) == "D♭maj7");
    CHECK (pop (*ChordNamer::bestReading ({ 8, 0, 3 }, 0, {}, &fMinor)) == "A♭/C");

    const auto eMajor = *Key::parse ("E major");
    CHECK (pop (*ChordNamer::bestReading ({ 1, 4, 8 }, 1, {}, &eMajor)) == "C♯m");
}

TEST_CASE ("Readings are ranked and include alternatives", "[theory][namer]")
{
    // C-E-G-A over A: Am7 and C6/A are both legitimate readings.
    NamingOptions off;
    off.preferSlash = false;
    const auto r = ChordNamer::readings ({ 0, 4, 7, 9 }, 9, off);
    REQUIRE (r.size() >= 2);
    CHECK (pop (r[0]) == "Am7");
    CHECK (pop (r[1]) == "C6/A");
}
