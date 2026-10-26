#include <catch2/catch_test_macros.hpp>

#include "core/midi/MidiWriter.h"
#include "core/reharm/Harmony.h"
#include "core/reharm/Reharmonizer.h"
#include "core/theory/ChordNamer.h"
#include "reharm_fixtures.h"

#include <chrono>
#include <cstdio>

using namespace so;
using namespace so::reharm;

namespace
{
    std::vector<analysis::AnalysisResult> fixtures()
    {
        return { so::test::fMinorLoop(), so::test::cMajorPop() };
    }

    double meanTension (const std::vector<Suggestion>& set)
    {
        double t = 0.0;
        for (const auto& s : set)
            t += s.tension;
        return set.empty() ? 0.0 : t / (double) set.size();
    }

    std::vector<std::uint8_t> bytesOf (const Suggestion& s)
    {
        return midi::writeSmf (s.laneList(), {});
    }

    /** All notes of a lane that start inside [start, end). */
    std::vector<midi::MidiNote> notesIn (const midi::MidiLane& lane, double start, double end)
    {
        std::vector<midi::MidiNote> out;
        for (const auto& n : lane.notes)
            if (n.startBeat >= start - 1.0e-9 && n.startBeat < end - 1.0e-9)
                out.push_back (n);
        return out;
    }

    bool sameNotes (const std::vector<midi::MidiNote>& a, const std::vector<midi::MidiNote>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].pitch != b[i].pitch || a[i].startBeat != b[i].startBeat || a[i].lengthBeats != b[i].lengthBeats
                || a[i].velocity != b[i].velocity)
                return false;
        return true;
    }
}

TEST_CASE ("Generates 6 to 12 suggestions, each listing its families", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
    {
        const auto set = generate (a, {}, {});
        CHECK (set.size() >= 6);
        CHECK (set.size() <= 12);
        std::set<std::string> ids;
        for (const auto& s : set)
        {
            CHECK_FALSE (s.techniques.empty());
            CHECK (s.families != 0);
            CHECK_FALSE (s.title.empty());
            CHECK_FALSE (s.summary.empty());
            CHECK (ids.insert (s.id).second);
            bool anyChanged = false;
            for (const auto& c : s.chords)
                anyChanged = anyChanged || c.changed;
            CHECK (anyChanged);
        }
    }
}

TEST_CASE ("Only enabled families are used", "[reharm][generate][acceptance]")
{
    const std::vector<unsigned> masks {
        familyBit (Family::Diatonic), familyBit (Family::Borrowed), familyBit (Family::Secondary),
        familyBit (Family::TritoneSub), familyBit (Family::ChromaticMediant), familyBit (Family::NeoRiemannian),
        familyBit (Family::Borrowed) | familyBit (Family::TritoneSub), allFamilies };
    for (const auto& a : fixtures())
        for (unsigned m : masks)
        {
            Settings s;
            s.families = m;
            for (const auto& sg : generate (a, s, {}))
            {
                CHECK ((sg.families & ~m) == 0);
                unsigned used = 0;
                for (const auto& c : sg.chords)
                    if (c.changed)
                    {
                        CHECK ((familyBit (familyOf (c.technique)) & m) != 0);
                        used |= familyBit (familyOf (c.technique));
                    }
                CHECK (used == sg.families);   // the card lists exactly what it used
            }
        }
}

TEST_CASE ("Raising tension raises average tension across generated sets", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
        for (std::uint64_t seed : { 1ull, 7ull, 42ull, 1000ull, 31337ull })
        {
            double prev = -1.0;
            double first = 0.0, last = 0.0;
            for (int t = 0; t <= 100; t += 20)
            {
                Settings s;
                s.tension = t;
                s.seed = seed;
                const double m = meanTension (generate (a, s, {}));
                CHECK (m >= prev);
                if (t == 0) first = m;
                last = m;
                prev = m;
            }
            CHECK (last > first);
        }
}

TEST_CASE ("Smoothness monotonically lowers voice movement across generated sets", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
    {
        double prev = 1.0e300;
        for (int sm = 0; sm <= 100; sm += 25)
        {
            Settings s;
            s.smoothness = sm;
            double total = 0.0;
            for (const auto& sg : generate (a, s, {}))
                total += sg.movement;
            CHECK (total <= prev + 1.0e-9);
            prev = total;
        }
    }
}

TEST_CASE ("No generated note falls outside the range limits", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
        for (auto bm : { BassMotion::Root, BassMotion::Inversions, BassMotion::Walking, BassMotion::Pedal })
            for (int density : { 3, 6 })
                for (auto [lo, hi] : { std::pair { 36, 84 }, std::pair { 50, 76 } })
                {
                    Settings s;
                    s.bassMotion = bm;
                    s.density = density;
                    s.rangeLow = lo;
                    s.rangeHigh = hi;
                    s.bassLow = 31;
                    s.bassHigh = 45;
                    s.melodyOnTop = density == 6 ? MelodyOnTop::Exact : MelodyOnTop::Off;
                    for (const auto& sg : generate (a, s, {}))
                    {
                        for (const auto& n : sg.lanes.chords.notes)
                        {
                            CHECK (n.pitch >= lo);
                            CHECK (n.pitch <= hi);
                        }
                        for (const auto& n : sg.lanes.bass.notes)
                        {
                            CHECK (n.pitch >= 31);
                            CHECK (n.pitch <= 45);
                        }
                        CHECK (sg.lanes.chords.notes.size() == sg.chords.size() * (size_t) density);
                    }
                }
}

TEST_CASE ("Locked segments are byte-identical across regenerations", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
    {
        const std::set<int> locked { 2, 5, (int) a.segments.size() };
        std::vector<Suggestion> all;
        for (std::uint64_t seed : { 1ull, 2ull, 99ull })
            for (int t : { 10, 90 })
                for (unsigned fam : { allFamilies, familyBit (Family::TritoneSub) | familyBit (Family::Secondary) })
                {
                    Settings s;
                    s.seed = seed;
                    s.tension = t;
                    s.families = fam;
                    s.smoothness = (int) (seed % 100);
                    for (auto& sg : generate (a, s, locked))
                        all.push_back (std::move (sg));
                }
        REQUIRE (all.size() > 10);

        for (const auto& seg : a.segments)
        {
            if (locked.count (seg.id) == 0)
                continue;
            const double s0 = seg.startBeat, s1 = seg.startBeat + seg.beats;
            const auto& ref = all.front();
            for (const auto& sg : all)
            {
                for (const auto& c : sg.chords)
                    if (c.segmentId == seg.id)
                    {
                        CHECK_FALSE (c.changed);
                        CHECK (c.chord == seg.chord);
                    }
                CHECK (sameNotes (notesIn (sg.lanes.chords, s0, s1), notesIn (ref.lanes.chords, s0, s1)));
                CHECK (sameNotes (notesIn (sg.lanes.bass, s0, s1), notesIn (ref.lanes.bass, s0, s1)));
                CHECK (sameNotes (notesIn (sg.lanes.top, s0, s1), notesIn (ref.lanes.top, s0, s1)));
            }
        }
    }
}

TEST_CASE ("Slash basses are never changed (except by Pedal)", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
        for (auto bm : { BassMotion::Root, BassMotion::Inversions, BassMotion::Walking })
        {
            Settings s;
            s.bassMotion = bm;
            for (int t : { 20, 80 })
            {
                s.tension = t;
                for (const auto& sg : generate (a, s, {}))
                    for (const auto& seg : a.segments)
                    {
                        if (! seg.chord.isSlash())
                            continue;
                        for (const auto& c : sg.chords)
                        {
                            if (c.segmentId != seg.id)
                                continue;
                            CHECK (c.chord.bassPc() == seg.chord.bassPc());
                            const auto bn = notesIn (sg.lanes.bass, c.startBeat, c.startBeat + 1.0e-3);
                            REQUIRE (bn.size() == 1);
                            CHECK (bn[0].pitch % 12 == seg.chord.bassPc());
                        }
                    }
            }
        }
}

TEST_CASE ("Strong-beat melody notes are chord tones or allowed tensions", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
        for (int t : { 0, 50, 100 })
        {
            Settings s;
            s.tension = t;
            for (const auto& sg : generate (a, s, {}))
                for (const auto& c : sg.chords)
                {
                    if (! c.changed)
                        continue;
                    for (const auto& h : melodyHits (a.melody, c.startBeat, c.beats, a.grid.beatsPerBar))
                        if (h.strong)
                        {
                            INFO (sg.title << ": " << c.symbol << " vs melody " << h.pitch);
                            CHECK (classifyTone (c.chord, h.pitch, t) != ToneRole::Avoid);
                        }
                }
        }
}

TEST_CASE ("Deterministic per seed; Shuffle changes the set", "[reharm][generate][acceptance]")
{
    for (const auto& a : fixtures())
    {
        Settings s;
        s.seed = 1234;
        s.bassMotion = BassMotion::Walking;
        const auto x = generate (a, s, {});
        const auto y = generate (a, s, {});
        REQUIRE (x.size() == y.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            CHECK (x[i].id == y[i].id);
            CHECK (x[i].title == y[i].title);
            CHECK (bytesOf (x[i]) == bytesOf (y[i]));
            for (size_t k = 0; k < x[i].chords.size(); ++k)
                CHECK (x[i].chords[k].explanation.detailed == y[i].chords[k].explanation.detailed);
        }

        bool differs = false;
        for (std::uint64_t seed : { 1ull, 2ull, 3ull, 4ull })
        {
            s.seed = seed;
            const auto z = generate (a, s, {});
            if (z.size() != x.size())
                differs = true;
            else
                for (size_t i = 0; i < z.size(); ++i)
                    differs = differs || z[i].id != x[i].id || bytesOf (z[i]) != bytesOf (x[i]);
        }
        CHECK (differs);
    }
}

TEST_CASE ("Every chord carries an explanation of at most 3 sentences", "[reharm][generate][explain]")
{
    for (const auto& a : fixtures())
        for (const auto& sg : generate (a, {}, {}))
            for (const auto& c : sg.chords)
            {
                CHECK_FALSE (c.roman.empty());
                CHECK_FALSE (c.explanation.simple.empty());
                CHECK (std::count (c.explanation.detailed.begin(), c.explanation.detailed.end(), '.') <= 3);
                CHECK (c.explanation.detailed.find ("\xE2\x80\x94") == std::string::npos);
                if (c.changed)
                    CHECK (c.explanation.simple.find ('(') != std::string::npos);
            }
}

TEST_CASE ("Timing: 12 suggestions over 32 bars", "[reharm][timing]")
{
    const auto a = so::test::fMinorLoop (4);   // 32 bars, 40 segments
    REQUIRE (a.bars == 32);
    Settings s;
    s.density = 5;
    s.bassMotion = BassMotion::Walking;
    double best = 1.0e300;
    size_t n = 0;
    for (int run = 0; run < 3; ++run)
    {
        s.seed = (std::uint64_t) run + 1;
        const auto t0 = std::chrono::steady_clock::now();
        const auto set = generate (a, s, {});
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min (best, std::chrono::duration<double, std::milli> (t1 - t0).count());
        n = set.size();
    }
    std::printf ("[reharm timing] %zu suggestions over 32 bars: best of 3 = %.1f ms\n", n, best);
    CHECK (n == 12);
    CHECK (best < 3000.0);   // loose guard for shared CI machines; the 300 ms target is tracked in ADR 0006
}

TEST_CASE ("Example output (F minor)", "[.example]")
{
    const auto a = so::test::fMinorLoop();
    Settings s;
    s.tension = 65;
    for (const auto& sg : generate (a, s, {}))
    {
        std::printf ("\n== %s  [tension %d, movement %.1f]\n%s\n", sg.title.c_str(), sg.tension, sg.movement, sg.summary.c_str());
        for (const auto& c : sg.chords)
            std::printf ("  %5.1f %-10s %-10s %s\n", c.startBeat, c.symbol.c_str(), c.roman.c_str(),
                         c.changed ? c.explanation.detailed.c_str() : "");
    }
}
