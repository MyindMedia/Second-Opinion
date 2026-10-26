#include <catch2/catch_test_macros.hpp>

#include "core/theory/Pitch.h"
#include "core/theory/ChordQuality.h"

using namespace so::theory;

TEST_CASE ("Pitch names parse and print", "[theory][pitch]")
{
    CHECK (parsePitch ("C")->pc() == 0);
    CHECK (parsePitch ("Db")->pc() == 1);
    CHECK (parsePitch ("D♭")->pc() == 1);
    CHECK (parsePitch ("C#")->pc() == 1);
    CHECK (parsePitch ("C♯")->pc() == 1);
    CHECK (parsePitch ("Bb")->pc() == 10);
    CHECK (parsePitch ("bb")->pc() == 10);
    CHECK (parsePitch ("Cb")->pc() == 11);
    CHECK (parsePitch ("E#")->pc() == 5);
    CHECK (parsePitch ("Ebb")->pc() == 2);
    CHECK (parsePitch ("F𝄪")->pc() == 7);
    CHECK_FALSE (parsePitch ("H").has_value());
    CHECK_FALSE (parsePitch ("").has_value());
    CHECK_FALSE (parsePitch ("C7").has_value());

    CHECK (SpelledPitch { Letter::D, -1 }.toString() == "D♭");
    CHECK (SpelledPitch { Letter::D, -1 }.toString (AccidentalStyle::Ascii) == "Db");
    CHECK (SpelledPitch { Letter::F, 1 }.toString() == "F♯");
}

TEST_CASE ("Spelling above a root follows letter names", "[theory][pitch]")
{
    const SpelledPitch eb { Letter::E, -1 };
    CHECK (SpelledPitch::above (eb, 4, 2).toString() == "G");     // major 3rd
    CHECK (SpelledPitch::above (eb, 3, 2).toString() == "G♭");    // minor 3rd
    CHECK (SpelledPitch::above (eb, 7, 4).toString() == "B♭");    // 5th

    const SpelledPitch fs { Letter::F, 1 };
    CHECK (SpelledPitch::above (fs, 4, 2).toString() == "A♯");
    CHECK (SpelledPitch::above (fs, 8, 4).toString() == "C𝄪");    // augmented 5th

    CHECK (SpelledPitch::fromPcAuto (6).toString() == "F♯");
    CHECK (SpelledPitch::fromPcAuto (10).toString() == "B♭");
}

TEST_CASE ("Interval labels", "[theory][interval]")
{
    CHECK (Interval { 0, 1 }.label() == "R");
    CHECK (Interval { 3, 3 }.label() == "♭3");
    CHECK (Interval { 4, 3 }.label() == "3");
    CHECK (Interval { 6, 5 }.label() == "♭5");
    CHECK (Interval { 8, 5 }.label() == "♯5");
    CHECK (Interval { 10, 7 }.label() == "♭7");
    CHECK (Interval { 9, 7 }.label() == "𝄫7");
    CHECK (Interval { 14, 9 }.label() == "9");
}
