#include <catch2/catch_test_macros.hpp>

#include "analysis_synth.h"
#include "core/analysis/Analyzer.h"
#include "core/analysis/ChordDecoder.h"
#include "core/analysis/KeyEstimator.h"
#include "core/theory/ChordNamer.h"

#include <iostream>

using namespace so::analysis;
using namespace so::theory;

namespace
{
    struct Bar { int bass; std::vector<int> upper; int rootPc; ChordQuality quality; int bassPc; };

    AudioClip progression (const std::vector<Bar>& bars, const BeatGrid& g, std::uint32_t seed, double sr = 44100.0,
                           double cents = 0.0, const std::vector<int>* melody = nullptr)
    {
        const double beatSec = 60.0 / g.bpm;
        const double barSec = beatSec * g.beatsPerBar;
        test::Synth s (g.downbeatSec + barSec * (double) bars.size() + 0.3, sr, seed);
        s.tuneCents = cents;
        for (size_t i = 0; i < bars.size(); ++i)
            s.chord (bars[i].bass, bars[i].upper, g.downbeatSec + barSec * (double) i, barSec);
        if (melody != nullptr)
            for (size_t i = 0; i < melody->size(); ++i)
                s.tone ((*melody)[i], g.downbeatSec + beatSec * (double) i, beatSec * 0.9, 0.12f, 4);
        s.noise (0.002f);
        return s.clip();
    }

    BeatFeatures beat (std::initializer_list<int> upper, int bassPc)
    {
        BeatFeatures b;
        for (int pc : upper)
            b.chroma[(size_t) pc] = 1.0f;
        if (bassPc >= 0)
            b.bass[(size_t) bassPc] = 1.0f;
        b.energy = 0.1f;
        b.silent = false;
        return b;
    }

    std::string sym (const Chord& c) { return ChordNamer::name (c, { NamingOptions::Style::Pop, true, NamingOptions::Spelling::Auto, AccidentalStyle::Ascii }); }
}

TEST_CASE ("Decoder: bass-aware readings on clean chroma", "[analysis][chords]")
{
    const ChordDecoder dec;
    ChordDecoderOptions o;

    auto decodeOne = [&] (const BeatFeatures& b)
    {
        auto segs = dec.decode ({ b, b, b, b }, o);
        REQUIRE (segs.size() == 1);
        return segs[0];
    };

    CHECK (sym (decodeOne (beat ({ 9, 0, 4, 7 }, 9)).chord) == "Am7");     // not C6/A
    CHECK (sym (decodeOne (beat ({ 0, 4, 7 }, 4)).chord) == "C/E");
    CHECK (sym (decodeOne (beat ({ 7, 11, 2 }, 5)).chord) == "G/F");       // foreign bass kept
    CHECK (sym (decodeOne (beat ({ 7, 11, 2 }, 11)).chord) == "G/B");
    CHECK (sym (decodeOne (beat ({ 7, 11, 2, 5 }, 7)).chord) == "G7");
    CHECK (sym (decodeOne (beat ({ 5, 9, 0, 4 }, 5)).chord) == "Fmaj7");
    CHECK (sym (decodeOne (beat ({ 2, 5, 9 }, -1)).chord) == "Dm");        // no bass: root position
    CHECK (sym (decodeOne (beat ({ 0, 3, 6, 9 }, 0)).chord) == "Cdim7");
    CHECK (sym (decodeOne (beat ({ 0, 5, 7 }, 0)).chord) == "Csus4");

    const auto seg = decodeOne (beat ({ 0, 4, 7 }, 0));
    CHECK (seg.confidence > 0.2f);
    CHECK (seg.confidence <= 1.0f);
    CHECK (seg.alternatives.size() == 3);
    for (const auto& a : seg.alternatives)
        CHECK (a.confidence <= seg.confidence);
}

TEST_CASE ("Decoder: silent beats extend the previous chord, leading silence stays empty", "[analysis][chords]")
{
    BeatFeatures silent;
    const auto c = beat ({ 0, 4, 7 }, 0);
    const auto g = beat ({ 7, 11, 2 }, 7);

    const auto segs = ChordDecoder().decode ({ silent, silent, silent, silent, c, c, silent, silent, g, g, g, g }, {});
    REQUIRE (segs.size() == 2);
    CHECK (segs[0].startBeat == 4.0);
    CHECK (segs[0].beats == 4.0);
    CHECK (sym (segs[0].chord) == "C");
    CHECK (segs[1].id == 1);
    CHECK (segs[1].startBeat == 8.0);
    CHECK (sym (segs[1].chord) == "G");

    CHECK (ChordDecoder().decode ({ silent, silent }, {}).empty());
}

TEST_CASE ("Decoder: harmonic rhythm follows strong beats", "[analysis][chords]")
{
    // C for 2 beats then F for 2 beats: changes on beat 3 of the bar.
    const auto c = beat ({ 0, 4, 7 }, 0);
    const auto f = beat ({ 5, 9, 0 }, 5);
    const auto segs = ChordDecoder().decode ({ c, c, f, f, c, c, c, c }, {});
    REQUIRE (segs.size() == 3);
    CHECK (segs[1].startBeat == 2.0);
    CHECK (segs[1].beats == 2.0);

    // A one-beat smear on a weak beat (Dm leaking over C, as from a passing melody) is absorbed.
    BeatFeatures smear = beat ({ 2, 5, 9 }, 0);
    for (int pc : { 0, 4, 7 })
        smear.chroma[(size_t) pc] = 0.6f;
    const auto blip = ChordDecoder().decode ({ c, c, c, smear, c, c, c, c }, {});
    CHECK (blip.size() == 1);
}

TEST_CASE ("Audio: Am7 | Fmaj7 | C/E | G at 100 BPM", "[analysis][chords][audio]")
{
    const BeatGrid g { 100.0, 4, 0.25 };
    const std::vector<Bar> bars {
        { 45, { 57, 60, 64, 67 }, 9, ChordQuality::Minor7, 9 },
        { 41, { 53, 57, 60, 64 }, 5, ChordQuality::Major7, 5 },
        { 40, { 60, 64, 67 }, 0, ChordQuality::Major, 4 },
        { 43, { 55, 59, 62 }, 7, ChordQuality::Major, 7 },
    };

    const auto r = analyze (progression (bars, g, 1234), g);
    REQUIRE (r.bars == 4);
    REQUIRE_FALSE (r.segments.empty());

    int rootFamilyOk = 0, exact = 0;
    for (int b = 0; b < 16; ++b)
    {
        const auto* seg = test::segmentAt (r, b + 0.5);
        if (seg == nullptr)
            continue;
        const auto& want = bars[(size_t) b / 4];
        if (seg->chord.root.pc() == want.rootPc && info (seg->chord.quality).family == info (want.quality).family)
            ++rootFamilyOk;
        if (seg->chord.root.pc() == want.rootPc && seg->chord.quality == want.quality && seg->chord.bassPc() == want.bassPc)
            ++exact;
    }

    std::cout << "[analysis] Am7|Fmaj7|C/E|G: root+family " << rootFamilyOk << "/16, exact " << exact << "/16, segments:";
    for (const auto& s : r.segments)
        std::cout << " " << sym (s.chord) << "@" << s.startBeat << "(" << s.confidence << ")";
    std::cout << "  key " << r.key.toString (AccidentalStyle::Ascii) << "\n";

    CHECK (rootFamilyOk >= 13);   // >= 80% of 16 beats

    const auto* slash = test::segmentAt (r, 8.5);
    REQUIRE (slash != nullptr);
    CHECK (sym (slash->chord) == "C/E");
}

TEST_CASE ("Audio: Am7 | Fmaj7 | C/E | G with a melody, detuned, at every quality", "[analysis][chords][audio]")
{
    const BeatGrid g { 100.0, 4, 0.0 };
    const std::vector<Bar> bars {
        { 45, { 57, 60, 64, 67 }, 9, ChordQuality::Minor7, 9 },
        { 41, { 53, 57, 60, 64 }, 5, ChordQuality::Major7, 5 },
        { 40, { 60, 64, 67 }, 0, ChordQuality::Major, 4 },
        { 43, { 55, 59, 62 }, 7, ChordQuality::Major, 7 },
    };
    // Melody with non-chord tones (D over Am7, B over C/E, A over G).
    const std::vector<int> melody { 76, 74, 72, 69, 72, 69, 65, 64, 67, 71, 72, 76, 74, 69, 71, 67 };

    for (auto [q, qname] : { std::pair { AnalysisQuality::Fast, "Fast" }, std::pair { AnalysisQuality::Balanced, "Balanced" },
                             std::pair { AnalysisQuality::Accurate, "Accurate" } })
    {
        AnalyzerOptions o;
        o.quality = q;
        const auto r = analyze (progression (bars, g, 555, 48000.0, 15.0, &melody), g, o);

        int ok = 0, exact = 0;
        for (int b = 0; b < 16; ++b)
        {
            const auto* seg = test::segmentAt (r, b + 0.5);
            if (seg == nullptr)
                continue;
            const auto& want = bars[(size_t) b / 4];
            if (seg->chord.root.pc() == want.rootPc && info (seg->chord.quality).family == info (want.quality).family)
                ++ok;
            if (seg->chord.root.pc() == want.rootPc && seg->chord.quality == want.quality && seg->chord.bassPc() == want.bassPc)
                ++exact;
        }
        std::cout << "[analysis] +15 cents, melody, " << qname << ": root+family " << ok << "/16, exact " << exact << "/16:";
        for (const auto& s : r.segments)
            std::cout << " " << sym (s.chord) << "@" << s.startBeat;
        std::cout << "\n";
        CHECK (ok >= 13);
    }
}

TEST_CASE ("Audio: key detection in three keys, spelled in key", "[analysis][key][audio]")
{
    const BeatGrid g { 100.0, 4, 0.0 };

    struct Case { const char* name; std::vector<Bar> bars; int tonic; Mode mode; std::vector<std::string> symbols; };
    const std::vector<Case> cases {
        { "C major", { { 36, { 60, 64, 67 }, 0, ChordQuality::Major, 0 },
                       { 41, { 60, 65, 69 }, 5, ChordQuality::Major, 5 },
                       { 43, { 59, 62, 65, 67 }, 7, ChordQuality::Dominant7, 7 },
                       { 36, { 60, 64, 67 }, 0, ChordQuality::Major, 0 } },
          0, Mode::Major, { "C", "F", "G7", "C" } },
        { "A minor", { { 45, { 57, 60, 64 }, 9, ChordQuality::Minor, 9 },
                       { 50, { 57, 62, 65 }, 2, ChordQuality::Minor, 2 },
                       { 40, { 56, 59, 62, 64 }, 4, ChordQuality::Dominant7, 4 },
                       { 45, { 57, 60, 64 }, 9, ChordQuality::Minor, 9 } },
          9, Mode::Minor, { "Am", "Dm", "E7", "Am" } },
        { "Eb major", { { 39, { 55, 58, 63 }, 3, ChordQuality::Major, 3 },
                        { 44, { 56, 60, 63 }, 8, ChordQuality::Major, 8 },
                        { 46, { 56, 58, 62, 65 }, 10, ChordQuality::Dominant7, 10 },
                        { 39, { 55, 58, 63 }, 3, ChordQuality::Major, 3 } },
          3, Mode::Major, { "Eb", "Ab", "Bb7", "Eb" } },
    };

    std::uint32_t seed = 100;
    for (const auto& c : cases)
    {
        DYNAMIC_SECTION (c.name)
        {
            const auto r = analyze (progression (c.bars, g, seed), g);
            std::cout << "[analysis] " << c.name << ": key " << r.key.toString (AccidentalStyle::Ascii)
                      << " (" << r.keyConfidence << "), chords";
            for (const auto& s : r.segments)
                std::cout << " " << sym (s.chord);
            std::cout << "\n";

            CHECK (r.key.tonic.pc() == c.tonic);
            CHECK (r.key.mode == c.mode);
            CHECK (r.keyConfidence > 0.3f);

            for (int bar = 0; bar < 4; ++bar)
            {
                const auto* seg = test::segmentAt (r, bar * 4 + 2.0);
                REQUIRE (seg != nullptr);
                CHECK (sym (seg->chord) == c.symbols[(size_t) bar]);
            }
        }
        ++seed;
    }
}

TEST_CASE ("Key estimator on a raw histogram", "[analysis][key]")
{
    std::array<double, 12> h {};
    for (int pc : { 2, 4, 6, 7, 9, 11, 1 })   // D major scale
        h[(size_t) pc] = 1.0;
    h[2] += 2.0;
    h[9] += 1.0;
    const auto e = KeyEstimator::estimate (h);
    CHECK (e.key.toString (AccidentalStyle::Ascii) == "D major");

    CHECK (KeyEstimator::estimate ({}).confidence == 0.0f);
    CHECK (KeyEstimator::keyFor (3, Mode::Minor).toString (AccidentalStyle::Ascii) == "Eb minor");
    CHECK (KeyEstimator::keyFor (6, Mode::Major).toString (AccidentalStyle::Ascii) == "F# major");
}
