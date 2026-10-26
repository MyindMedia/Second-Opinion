#include <catch2/catch_test_macros.hpp>

#include "core/reharm/BassGenerator.h"
#include "core/reharm/Harmony.h"
#include "core/reharm/Reharmonizer.h"
#include "core/reharm/VoiceLeadingSolver.h"
#include "reharm_fixtures.h"

#include <set>

using namespace so;
using namespace so::reharm;
using so::test::chord;

namespace
{
    std::vector<VoicingRequest> requestsFor (const analysis::AnalysisResult& a, int bass = 40)
    {
        std::vector<VoicingRequest> r;
        for (const auto& s : a.segments)
            r.push_back ({ s.chord, bass, std::nullopt, false });
        return r;
    }

    int totalMovement (const std::vector<std::vector<int>>& v)
    {
        int t = 0;
        for (size_t i = 1; i < v.size(); ++i)
            t += VoiceLeadingSolver::movement (v[i - 1], v[i]);
        return t;
    }
}

TEST_CASE ("Solver voicings stay in range, at the requested density, above the bass", "[reharm][solver]")
{
    for (const auto& a : { so::test::fMinorLoop(), so::test::cMajorPop() })
        for (int density = 3; density <= 6; ++density)
            for (auto [lo, hi] : { std::pair { 36, 84 }, std::pair { 48, 72 }, std::pair { 55, 79 } })
            {
                VoiceLeadingSolver solver ({ lo, hi, density, 60 });
                const auto reqs = requestsFor (a, 41);
                const auto v = solver.solve (reqs);
                REQUIRE (v.size() == reqs.size());
                for (size_t i = 0; i < v.size(); ++i)
                {
                    CHECK ((int) v[i].size() == density);
                    CHECK (std::is_sorted (v[i].begin(), v[i].end()));
                    CHECK (v[i].front() >= lo);
                    CHECK (v[i].back() <= hi);
                    CHECK (v[i].front() > 41);
                    // Every essential guide tone (3rd or sus tone) is present.
                    std::set<int> pcs;
                    for (int n : v[i]) pcs.insert (n % 12);
                    const auto tones = reqs[i].chord.chordTonePcs();
                    CHECK (pcs.count (tones[1]) == 1);
                }
            }
}

TEST_CASE ("Smoothness monotonically lowers total voice movement", "[reharm][solver][acceptance]")
{
    for (const auto& a : { so::test::fMinorLoop(), so::test::cMajorPop(), so::test::fMinorLoop (2) })
        for (int density : { 3, 4, 5 })
        {
            int prev = 1 << 30;
            int first = 0, last = 0;
            for (int s = 0; s <= 100; s += 10)
            {
                VoiceLeadingSolver solver ({ 36, 84, density, s });
                const int m = totalMovement (solver.solve (requestsFor (a)));
                CHECK (m <= prev);
                if (s == 0) first = m;
                last = m;
                prev = m;
            }
            CHECK (last <= first);
        }
}

TEST_CASE ("Locked requests keep their isolated voicing whatever the neighbours", "[reharm][solver]")
{
    VoiceLeadingSolver solver ({ 36, 84, 4, 80 });
    std::vector<VoicingRequest> a { { chord ("C"), 36, std::nullopt, false }, { chord ("Am7"), 33, std::nullopt, true },
                                    { chord ("F"), 41, std::nullopt, false } };
    auto b = a;
    b[0].chord = chord ("E7");
    b[2].chord = chord ("Bb");
    CHECK (solver.solve (a)[1] == solver.solve (b)[1]);
    CHECK (solver.solve (a)[1] == solver.candidates (a[1]).front().notes);
}

TEST_CASE ("Melody on top puts the melody note in the top voice", "[reharm][solver]")
{
    VoiceLeadingSolver solver ({ 36, 84, 4, 60 });
    const auto v = solver.solve ({ { chord ("Fm9"), 41, 72, false }, { chord ("Dbmaj7"), 37, 65, false },
                                   { chord ("Bbm7"), 34, 73, false } });
    CHECK (v[0].back() == 72);
    CHECK (v[1].back() == 65);
    CHECK (v[2].back() == 73);

    auto a = so::test::fMinorLoop();
    Settings s;
    s.melodyOnTop = MelodyOnTop::Exact;
    const auto sg = voiceOriginal (a, s, {});
    for (size_t i = 0; i < sg.chords.size(); ++i)
    {
        const auto m = melodyAt (a.melody, sg.chords[i].startBeat, 1.0);
        REQUIRE (m.has_value());
        CHECK (sg.lanes.top.notes[i].pitch == *m);
    }

    s.melodyOnTop = MelodyOnTop::ThirdBelow;
    const auto third = voiceOriginal (a, s, {});
    for (size_t i = 0; i < third.chords.size(); ++i)
    {
        const auto m = melodyAt (a.melody, third.chords[i].startBeat, 1.0);
        const int d = *m - third.lanes.top.notes[i].pitch;
        CHECK ((d == 3 || d == 4));
    }
}

TEST_CASE ("Bass styles", "[reharm][bass]")
{
    const auto key = *theory::Key::parse ("C major");
    const std::vector<BassSpan> spans { { chord ("C"), 0, 4, false }, { chord ("G/B"), 4, 4, false },
                                        { chord ("Am7"), 8, 4, false }, { chord ("F"), 12, 2, false },
                                        { chord ("G7"), 14, 2, false }, { chord ("C"), 16, 4, false } };
    Settings s;

    SECTION ("Root plays the written bass, slash kept")
    {
        s.bassMotion = BassMotion::Root;
        const auto b = BassGenerator::generate (spans, s, key, 4);
        REQUIRE (b.notes.size() == spans.size());
        for (size_t i = 0; i < spans.size(); ++i)
        {
            CHECK (b.notes[i].pitch % 12 == spans[i].chord.bassPc());
            CHECK (b.notes[i].pitch >= 28);
            CHECK (b.notes[i].pitch <= 48);
        }
    }
    SECTION ("Inversions follow chord tones and keep the slash")
    {
        s.bassMotion = BassMotion::Inversions;
        const auto b = BassGenerator::generate (spans, s, key, 4);
        REQUIRE (b.notes.size() == spans.size());
        CHECK (b.notes[1].pitch % 12 == 11);   // G/B
        int maxLeap = 0;
        for (size_t i = 0; i < spans.size(); ++i)
        {
            const auto pcs = spans[i].chord.chordTonePcs();
            CHECK (std::find (pcs.begin(), pcs.end(), b.notes[i].pitch % 12) != pcs.end());
            if (i > 0) maxLeap = std::max (maxLeap, std::abs (b.notes[i].pitch - b.notes[i - 1].pitch));
        }
        CHECK (maxLeap <= 7);
    }
    SECTION ("Walking fills every beat and approaches each change")
    {
        s.bassMotion = BassMotion::Walking;
        const auto b = BassGenerator::generate (spans, s, key, 4);
        CHECK (b.notes.size() == 20);
        for (size_t i = 0; i < spans.size(); ++i)
        {
            const auto it = std::find_if (b.notes.begin(), b.notes.end(),
                                          [&] (const midi::MidiNote& n) { return n.startBeat == spans[i].startBeat; });
            REQUIRE (it != b.notes.end());
            CHECK (it->pitch % 12 == spans[i].chord.bassPc());
            CHECK (it->pitch == b.spanBass[i]);
        }
        for (size_t i = 0; i + 1 < spans.size(); ++i)
        {
            const auto it = std::find_if (b.notes.begin(), b.notes.end(), [&] (const midi::MidiNote& n)
                                          { return n.startBeat == spans[i + 1].startBeat - 1.0; });
            REQUIRE (it != b.notes.end());
            const int d = std::abs (it->pitch - b.spanBass[i + 1]);
            CHECK ((d <= 2 || d == 5 || d == 7));
        }
        for (const auto& n : b.notes)
        {
            CHECK (n.pitch >= 28);
            CHECK (n.pitch <= 48);
        }
    }
    SECTION ("Pedal holds the chosen note over N bars")
    {
        s.bassMotion = BassMotion::Pedal;
        s.pedalPc = 7;
        s.pedalBars = 2;
        const auto b = BassGenerator::generate (spans, s, key, 4);
        REQUIRE (b.notes.size() == 3);
        for (const auto& n : b.notes)
            CHECK (n.pitch % 12 == 7);
        CHECK (b.notes[0].lengthBeats == 8.0);
        CHECK (b.notes[2].lengthBeats == 4.0);
    }
}
