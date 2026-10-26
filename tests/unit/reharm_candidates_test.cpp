#include <catch2/catch_test_macros.hpp>

#include "core/reharm/CandidateGenerator.h"
#include "core/reharm/Explainer.h"
#include "core/reharm/Harmony.h"
#include "core/theory/ChordNamer.h"
#include "reharm_fixtures.h"

using namespace so;
using namespace so::reharm;
using so::test::chord;

namespace
{
    std::string nm (const theory::Chord& c)
    {
        return theory::ChordNamer::name (c, { theory::NamingOptions::Style::Pop, true,
                                              theory::NamingOptions::Spelling::Auto, theory::AccidentalStyle::Ascii });
    }
    const Key cMajor = *Key::parse ("C major");
    const Key fMinor = *Key::parse ("F minor");
}

TEST_CASE ("Chord transformations per family", "[reharm][candidates]")
{
    CHECK (nm (tritoneSubstitute (chord ("G7"), cMajor)) == "Db7");
    CHECK (nm (tritoneSubstitute (chord ("C7"), fMinor)) == "Gb7");
    CHECK (nm (tritoneSubstitute (chord ("D9"), cMajor)) == "Ab9");

    CHECK (nm (secondaryDominantOf (chord ("Dm"), cMajor)) == "A7");
    CHECK (nm (secondaryDominantOf (chord ("Em"), cMajor)) == "B7");
    CHECK (nm (secondaryDominantOf (chord ("G"), cMajor)) == "D7");
    CHECK (nm (secondaryDominantOf (chord ("Bbm7"), fMinor)) == "F7");

    CHECK (nm (neoP (chord ("C"), cMajor)) == "Cm");
    CHECK (nm (neoL (chord ("C"), cMajor)) == "Em");
    CHECK (nm (neoR (chord ("C"), cMajor)) == "Am");
    CHECK (nm (neoP (chord ("Cm"), cMajor)) == "C");
    CHECK (nm (neoL (chord ("Cm"), cMajor)) == "Ab");
    CHECK (nm (neoR (chord ("Cm"), cMajor)) == "Eb");
    CHECK (nm (neoR (chord ("Fm7"), fMinor, true)) == "Abmaj7");

    std::vector<std::string> meds;
    for (const auto& m : chromaticMediants (chord ("C"), cMajor))
        meds.push_back (nm (m));
    CHECK (meds == std::vector<std::string> { "E", "Ab", "Eb", "A" });

    CHECK (nm (diatonicChord (cMajor, 1, true)) == "Dm7");
    CHECK (nm (diatonicChord (cMajor, 4, true)) == "G7");
    CHECK (nm (diatonicChord (fMinor, 5, true)) == "Dbmaj7");
}

TEST_CASE ("Melody tone roles follow the tension knob", "[reharm][melody]")
{
    CHECK (classifyTone (chord ("C"), 4, 0) == ToneRole::ChordTone);
    CHECK (classifyTone (chord ("C"), 2, 0) == ToneRole::Tension);       // 9th
    CHECK (classifyTone (chord ("C"), 5, 100) == ToneRole::Avoid);       // natural 11 over major
    CHECK (classifyTone (chord ("G7"), 8, 10) == ToneRole::Avoid);       // b9 needs tension
    CHECK (classifyTone (chord ("G7"), 8, 50) == ToneRole::Tension);
    CHECK (classifyTone (chord ("G/F"), 5, 0) == ToneRole::ChordTone);   // the slash bass sounds

    CHECK (isStrongBeat (0.0, 4));
    CHECK_FALSE (isStrongBeat (1.0, 4));
    CHECK (isStrongBeat (6.0, 4));
    CHECK_FALSE (isStrongBeat (2.5, 4));
}

TEST_CASE ("Tension metric orders chords sensibly", "[reharm][tension]")
{
    CHECK (chordTension (chord ("Fm"), fMinor) < chordTension (chord ("Fm9"), fMinor));
    CHECK (chordTension (chord ("C7"), fMinor) < chordTension (chord ("Gb7"), fMinor));
    CHECK (chordTension (chord ("C"), cMajor) < chordTension (chord ("Ab"), cMajor));
    CHECK (targetChordTension (0) < targetChordTension (100));
}

TEST_CASE ("Candidates use only enabled families and respect slash and lock", "[reharm][candidates]")
{
    const auto a = so::test::fMinorLoop();
    Settings s;
    s.tension = 70;
    s.families = familyBit (Family::TritoneSub);
    const auto cands = CandidateGenerator::generate (a, s, { 10 });

    bool sawGb7 = false;
    for (const auto& c : cands)
    {
        CHECK (familyOf (c.technique) == Family::TritoneSub);
        CHECK (a.segments[(size_t) c.segmentIndex].id != 10);   // locked
        if (c.technique == Technique::TritoneSub && c.segmentIndex == 9)
            sawGb7 = true;
        if (c.technique == Technique::TritoneSub && c.segmentIndex == 8)
            FAIL ("C7sus4 resolves to C7, not down a fifth, so it has no tritone sub here");
    }
    CHECK_FALSE (sawGb7);   // segment 10 (C7) is locked

    const auto open = CandidateGenerator::generate (a, s, {});
    bool found = false;
    for (const auto& c : open)
        if (c.technique == Technique::TritoneSub && c.segmentIndex == 9)
        {
            found = true;
            CHECK (nm (c.spans.front().chord).rfind ("Gb", 0) == 0);
        }
    CHECK (found);

    // Slash segment (Ab/C, index 2): every replacement keeps C in the bass, nothing is inserted.
    s.families = allFamilies;
    for (const auto& c : CandidateGenerator::generate (a, s, {}))
        if (c.segmentIndex == 2)
        {
            CHECK_FALSE (c.insertion);
            for (const auto& sp : c.spans)
                CHECK (sp.chord.bassPc() == 0);
        }
}

TEST_CASE ("Secondary dominant and ii-V candidates target the next chord", "[reharm][candidates]")
{
    const auto a = so::test::cMajorPop();
    Settings s;
    s.tension = 60;
    s.families = familyBit (Family::Secondary);
    bool sawA7intoDm = false, sawIiV = false;
    for (const auto& c : CandidateGenerator::generate (a, s, {}))
    {
        CHECK (familyOf (c.technique) == Family::Secondary);
        if (c.segmentIndex == 5 && c.technique == Technique::SecondaryDominant)   // Em7 before Dm7
            sawA7intoDm = nm (c.spans.back().chord).rfind ("A", 0) == 0 || sawA7intoDm;
        if (c.technique == Technique::IiVInsertion)
            sawIiV = true;
    }
    CHECK (sawA7intoDm);
    CHECK (sawIiV);
}

TEST_CASE ("Explainer text is plain first, theory in parentheses, at most 3 sentences", "[reharm][explain]")
{
    ExplainContext ctx;
    ctx.chord = chord ("Gb7");
    ctx.original = chord ("C7");
    ctx.next = chord ("Fm9");
    ctx.key = fMinor;
    ctx.technique = Technique::TritoneSub;
    ctx.melodyPitch = 64;
    const auto e = Explainer::explain (ctx);

    CHECK (e.roman == "subV7");
    CHECK (e.function == theory::HarmonicFunction::Dominant);
    CHECK (e.simple.rfind ("Swaps C7 for G♭7, which slides down a half step into Fm9 (tritone substitution, subV7).", 0) == 0);
    CHECK (e.melodyRelation == "The melody E is the 7th of G♭7.");
    REQUIRE_FALSE (e.voiceLeadingNotes.empty());

    auto sentences = [] (const std::string& t) { return std::count (t.begin(), t.end(), '.'); };
    CHECK (sentences (e.simple) <= 3);
    CHECK (sentences (e.detailed) <= 3);
    CHECK (e.detailed.size() > e.simple.size());
    CHECK (e.detailed.find ("\xE2\x80\x94") == std::string::npos);   // no em dash
}
