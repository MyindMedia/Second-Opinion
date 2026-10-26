#include <catch2/catch_test_macros.hpp>

#include "core/theory/ChordSymbolParser.h"
#include "core/theory/RomanNumeral.h"

using namespace so::theory;

namespace
{
    RomanNumeral rn (std::string_view chord, std::string_view key)
    {
        auto c = ChordSymbolParser::tryParse (chord);
        auto k = Key::parse (key);
        REQUIRE (c.has_value());
        REQUIRE (k.has_value());
        return RomanNumeral::analyze (*c, *k);
    }
}

TEST_CASE ("Key parsing and scales", "[theory][key]")
{
    const auto f = Key::parse ("F minor");
    REQUIRE (f.has_value());
    CHECK (f->mode == Mode::Minor);
    CHECK (f->toString() == "F minor");

    const auto sc = f->scale();
    CHECK (sc[2].toString() == "A♭");
    CHECK (sc[6].toString() == "E♭");
    CHECK (f->signature() == -4);

    CHECK (Key::parse ("Bb")->toString() == "B♭ major");
    CHECK (Key::parse ("c# minor")->toString() == "C♯ minor");
    CHECK (Key::parse ("Em")->mode == Mode::Minor);
    CHECK (Key::parse ("D major")->signature() == 2);
    CHECK_FALSE (Key::parse ("Q major").has_value());
    CHECK_FALSE (Key::parse ("C lydian-ish").has_value());

    CHECK (Key::parse ("D major")->spell (6).toString() == "F♯");
    CHECK (Key::parse ("F major")->spell (10).toString() == "B♭");
}

TEST_CASE ("Roman numerals in C major", "[theory][roman]")
{
    CHECK (rn ("C", "C major").numeral == "I");
    CHECK (rn ("Dm7", "C major").numeral == "ii7");
    CHECK (rn ("Em", "C major").numeral == "iii");
    CHECK (rn ("Fmaj7", "C major").numeral == "IVmaj7");
    CHECK (rn ("G7", "C major").numeral == "V7");
    CHECK (rn ("Am", "C major").numeral == "vi");
    CHECK (rn ("Bm7b5", "C major").numeral == "viiø7");

    CHECK (rn ("G7", "C major").function == HarmonicFunction::Dominant);
    CHECK (rn ("Dm7", "C major").function == HarmonicFunction::Predominant);
    CHECK (rn ("Am", "C major").function == HarmonicFunction::Tonic);
    CHECK (rn ("C", "C major").diatonic);
}

TEST_CASE ("Borrowed, secondary and tritone-sub chords", "[theory][roman]")
{
    const auto fm = rn ("Fm", "C major");
    CHECK (fm.numeral == "iv");
    CHECK (fm.borrowed);
    CHECK (fm.technique() == "Modal interchange");

    const auto ab = rn ("Ab", "C major");
    CHECK (ab.numeral == "♭VI");
    CHECK (ab.borrowed);
    CHECK (ab.function == HarmonicFunction::Predominant);

    const auto bb7 = rn ("Bb7", "C major");
    CHECK (bb7.numeral == "♭VII7");
    CHECK (bb7.borrowed);
    CHECK (bb7.function == HarmonicFunction::Dominant); // backdoor dominant

    const auto d7 = rn ("D7", "C major");
    CHECK (d7.numeral == "V7/V");
    CHECK (d7.secondary);

    CHECK (rn ("E7", "C major").numeral == "V7/vi");
    CHECK (rn ("A", "C major").numeral == "V/ii");

    const auto db7 = rn ("Db7", "C major");
    CHECK (db7.numeral == "subV7");
    CHECK (db7.tritoneSub);
    CHECK (db7.technique() == "Tritone substitution");

    CHECK (rn ("Ab7", "C major").numeral == "subV7/V");
}

TEST_CASE ("Roman numerals in F minor (the brief's mock key)", "[theory][roman]")
{
    CHECK (rn ("Fm9", "F minor").numeral == "i9");
    CHECK (rn ("Dbmaj7", "F minor").numeral == "VImaj7");
    CHECK (rn ("Ab/C", "F minor").numeral == "III");
    CHECK (rn ("Eb", "F minor").numeral == "VII");
    CHECK (rn ("Ebsus4", "F minor").numeral == "VIIsus4");
    CHECK (rn ("Bbm7", "F minor").numeral == "iv7");

    const auto c7 = rn ("C7", "F minor");
    CHECK (c7.numeral == "V7");
    CHECK (c7.diatonic);
    CHECK (c7.function == HarmonicFunction::Dominant);

    CHECK (rn ("Edim7", "F minor").numeral == "vii°7");

    const auto bb = rn ("Bb", "F minor");
    CHECK (bb.numeral == "IV");
    CHECK (bb.borrowed); // Dorian/major IV, from F major
}

TEST_CASE ("ASCII numerals", "[theory][roman]")
{
    auto c = *ChordSymbolParser::tryParse ("Ab");
    CHECK (RomanNumeral::analyze (c, *Key::parse ("C major"), AccidentalStyle::Ascii).numeral == "bVI");
}
