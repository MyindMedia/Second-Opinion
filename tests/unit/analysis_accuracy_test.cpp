// Accuracy fixes measured on the P2-05 eval set: tonal-centre key choice (relative major/minor),
// global tuning estimation and bass-aware harmonic suppression for slash chords.

#include <catch2/catch_test_macros.hpp>

#include "analysis_synth.h"
#include "core/analysis/Analyzer.h"
#include "core/analysis/ChromaExtractor.h"
#include "core/analysis/FFT.h"
#include "core/analysis/KeyEstimator.h"
#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"

#include <cmath>
#include <iostream>
#include <numbers>

using namespace so::analysis;
using namespace so::theory;

namespace
{
    std::string sym (const Chord& c) { return ChordNamer::name (c, { NamingOptions::Style::Pop, true, NamingOptions::Spelling::Auto, AccidentalStyle::Ascii }); }

    struct Sym { const char* symbol; double beats; };

    std::vector<ChordSegment> segments (const std::vector<Sym>& prog)
    {
        std::vector<ChordSegment> out;
        double beat = 0.0;
        for (const auto& p : prog)
        {
            ChordSegment s;
            s.id = (int) out.size();
            s.startBeat = beat;
            s.beats = p.beats;
            s.chord = *ChordSymbolParser::tryParse (p.symbol);
            out.push_back (s);
            beat += p.beats;
        }
        return out;
    }

    /** One clean beat per chord beat: chord tones in the upper chroma, the bass in bass chroma. */
    std::vector<BeatFeatures> beatsFor (const std::vector<ChordSegment>& segs)
    {
        std::vector<BeatFeatures> out;
        for (const auto& s : segs)
            for (int b = 0; b < (int) s.beats; ++b)
            {
                BeatFeatures f;
                for (int pc : s.chord.chordTonePcs())
                    f.chroma[(size_t) pc] = 1.0f;
                f.bass[(size_t) s.chord.bassPc()] = 1.0f;
                f.energy = 0.1f;
                f.silent = false;
                out.push_back (f);
            }
        return out;
    }

    /** A note whose partial h has amplitude rho^(h-1) (rho 0.5 is dull, 0.9 is an overdriven bass). */
    void brightTone (std::vector<float>& buf, double sr, int midi, double start, double dur, float amp,
                     double rho)
    {
        const double f0 = test::midiHz (midi);
        const auto s0 = (size_t) (start * sr);
        for (size_t i = 0; i < (size_t) (dur * sr) && s0 + i < buf.size(); ++i)
        {
            const double t = (double) i / sr;
            const double env = std::exp (-0.6 * t) * std::min (1.0, t / 0.01) * std::min (1.0, (dur - t) / 0.03);
            double v = 0.0;
            for (int h = 1; h <= 10 && f0 * h < 0.45 * sr; ++h)
                v += std::pow (rho, h - 1) * std::sin (2.0 * std::numbers::pi * f0 * h * t + h * 0.7);
            buf[s0 + i] += (float) (amp * env * v);
        }
    }

    /** Four bars of one chord: a bright bass under a quiet close-position upper triad. */
    AnalysisResult barsOver (int bassMidi, std::vector<int> upper, double rho, float bassAmp, std::uint32_t seed)
    {
        const BeatGrid g { 90.0, 4, 0.0 };
        const double bar = 4 * 60.0 / g.bpm;
        test::Synth s (bar * 4 + 0.3, 22050.0, seed);
        for (int b = 0; b < 4; ++b)
        {
            brightTone (s.buf, s.sr, bassMidi, b * bar, bar, bassAmp, rho);
            for (int m : upper)
                s.tone (m, b * bar, bar, 0.08f, 4);
        }
        s.noise (0.001f);
        return analyze (s.clip(), g);
    }
}

TEST_CASE ("Key: tonal centre separates F minor from Ab major on the same notes", "[analysis][key]")
{
    // The STATUS.md loop: every chord holds Ab, C or Eb, so the profile alone prefers Ab major.
    const auto fMinor = segments ({ { "Fm9", 4 }, { "Dbmaj7", 4 }, { "Ab/C", 4 }, { "Ebsus4", 2 }, { "Eb", 2 },
                                    { "Fm9", 4 }, { "Dbmaj7", 4 }, { "Ab/C", 4 }, { "Ebsus4", 2 }, { "Eb", 2 } });
    const auto fm = *Key::parse ("F minor");
    const auto ab = *Key::parse ("Ab major");
    CHECK (KeyEstimator::tonalCentreEvidence (fm, fMinor) > KeyEstimator::tonalCentreEvidence (ab, fMinor) + 0.05f);

    const auto profileOnly = KeyEstimator::estimate (KeyEstimator::histogram (beatsFor (fMinor), &fMinor));
    const auto withCentre = KeyEstimator::estimateWithChords (beatsFor (fMinor), fMinor);
    std::cout << "[analysis] F minor loop: profile " << profileOnly.key.toString (AccidentalStyle::Ascii)
              << ", with tonal centre " << withCentre.key.toString (AccidentalStyle::Ascii) << "\n";
    CHECK (withCentre.key.toString (AccidentalStyle::Ascii) == "F minor");

    // Same chord family, but it starts and ends on Ab with V-I cadences.
    const auto abMajor = segments ({ { "Ab", 4 }, { "Db", 4 }, { "Eb", 4 }, { "Fm", 4 },
                                     { "Ab", 4 }, { "Db", 4 }, { "Eb", 4 }, { "Ab", 4 } });
    CHECK (KeyEstimator::estimateWithChords (beatsFor (abMajor), abMajor).key.toString (AccidentalStyle::Ascii) == "Ab major");

    // A loop ending on V does not hand the key to the dominant (only relatives compete).
    const auto endsOnV = segments ({ { "Am7", 4 }, { "Fmaj7", 4 }, { "C/E", 4 }, { "G", 4 } });
    const auto k = KeyEstimator::estimateWithChords (beatsFor (endsOnV), endsOnV).key.toString (AccidentalStyle::Ascii);
    CHECK ((k == "A minor" || k == "C major"));

    // Aeolian bVII-i: Dm C Bb C Dm reads D minor, not F major.
    const auto aeolian = segments ({ { "Dm", 4 }, { "C", 4 }, { "Bb", 4 }, { "C", 4 }, { "Dm", 4 } });
    CHECK (KeyEstimator::estimateWithChords (beatsFor (aeolian), aeolian).key.toString (AccidentalStyle::Ascii) == "D minor");
}

TEST_CASE ("Audio: the F minor loop reads F minor with Ab/C intact", "[analysis][key][audio]")
{
    // Voicings from SimpleVoicer for this loop (the STATUS.md end-to-end idea), 92 BPM.
    const BeatGrid g { 92.0, 4, 0.0 };
    const double beat = 60.0 / g.bpm;
    struct C { int bass; std::vector<int> up; double beats; };
    const std::vector<C> loop { { 41, { 56, 63, 67, 77 }, 4 }, { 37, { 61, 65, 68, 72 }, 4 }, { 36, { 60, 63, 68 }, 4 },
                                { 39, { 58, 63, 68 }, 2 }, { 39, { 58, 63, 67 }, 2 } };
    test::Synth s (beat * 32 + 0.3, 44100.0, 4242);
    double t = 0.0;
    for (int rep = 0; rep < 2; ++rep)
        for (const auto& c : loop)
        {
            s.chord (c.bass, c.up, t, c.beats * beat);
            t += c.beats * beat;
        }
    s.noise (0.002f);

    const auto r = analyze (s.clip(), g);
    std::cout << "[analysis] F minor loop audio: key " << r.key.toString (AccidentalStyle::Ascii) << ", chords";
    for (const auto& seg : r.segments)
        std::cout << " " << sym (seg.chord);
    std::cout << "\n";
    CHECK (r.key.toString (AccidentalStyle::Ascii) == "F minor");
    const auto* slash = test::segmentAt (r, 10.0);
    REQUIRE (slash != nullptr);
    CHECK (sym (slash->chord) == "Ab/C");
}

TEST_CASE ("Tuning: global offset from A440 is estimated and applied", "[analysis][tuning]")
{
    for (double cents : { 0.0, 35.0, -45.0, 20.0 })
    {
        test::Synth s (3.0, 22050.0, 77);
        s.tuneCents = cents;
        s.chord (43, { 59, 62, 67 }, 0.0, 1.5);
        s.chord (36, { 60, 64, 67 }, 1.5, 1.5);
        s.noise (0.001f);
        const float est = ChromaExtractor::estimateTuning (s.buf, s.sr);
        INFO ("detune " << cents << " estimated " << est);
        CHECK (std::abs (est - cents) <= 5.0);
    }
    CHECK (ChromaExtractor::estimateTuning (std::vector<float> (22050, 0.0f), 22050.0) == 0.0f);

    // A whole song 45 cents sharp (nearly a quarter tone) still reads in key, with tuning reported.
    const BeatGrid g { 100.0, 4, 0.0 };
    test::Synth s (g.beatToSec (16) + 0.3, 44100.0, 31);
    s.tuneCents = 45.0;
    const int bass[4] { 43, 40, 36, 38 };
    const std::vector<int> up[4] { { 55, 59, 62 }, { 55, 59, 64 }, { 55, 60, 64 }, { 54, 57, 62 } };
    for (int b = 0; b < 4; ++b)
        s.chord (bass[b], up[b], g.beatToSec (b * 4), g.beatToSec (4));
    s.noise (0.002f);
    const auto r = analyze (s.clip(), g);
    CHECK (std::abs (r.tuningCents - 45.0f) <= 5.0f);
    const char* want[4] { "G", "Em", "C", "D" };
    for (int b = 0; b < 4; ++b)
    {
        const auto* seg = test::segmentAt (r, b * 4 + 2.0);
        REQUIRE (seg != nullptr);
        CHECK (sym (seg->chord) == want[b]);
    }
}

TEST_CASE ("Salience: a bright bass's partials are removed, not read as chord tones", "[analysis][chroma]")
{
    // C3 with partials at 0.8^(h-1) under Ab C Eb: its 3rd partial is G4, a fake fifth for Cm.
    const double sr = 22050.0;
    const int n = 8192;
    std::vector<float> frame ((size_t) n);
    double winSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = 2.0 * std::numbers::pi * i / n;
        const double w = 0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x);
        winSum += w;
        const double t = i / sr;
        double v = 0.0;
        for (int h = 1; h <= 10; ++h)
            v += 0.2 * std::pow (0.8, h - 1) * std::sin (2.0 * std::numbers::pi * test::midiHz (48) * h * t + h);
        for (int m : { 60, 63, 68 })
            for (int h = 1; h <= 4; ++h)
                v += 0.08 / h * std::sin (2.0 * std::numbers::pi * test::midiHz (m) * h * t + m * h);
        frame[(size_t) i] = (float) (v * w);
    }
    std::vector<float> mags ((size_t) n / 2 + 1);
    std::vector<FFT::Complex> scratch;
    FFT (FFT::orderFor (n)).realMagnitudes (frame.data(), mags.data(), scratch);
    const auto p = ChromaExtractor::pitchSalience (mags, n, sr, (float) (2.0 / winSum));

    auto at = [&] (int midi) { return p[(size_t) (midi - kLowestPitch)]; };
    CHECK (at (48) > 0.15f);           // the bass itself
    // G4 (the 3rd partial, 0.128) loses most of its level: the old fixed 1/h model left 0.075,
    // more than the real Ab4 above it after log compression. The decoder handles the rest.
    CHECK (at (67) < 0.06f);
    CHECK (at (67) < 0.6f * at (68));
    CHECK (at (63) > 0.05f);
    CHECK (at (68) > 0.05f);
}

TEST_CASE ("Audio: slash chords over a bright bass keep their root", "[analysis][chords][audio]")
{
    int ok = 0, total = 0;
    for (int bassMidi : { 36, 48 })
        for (double rho : { 0.5, 0.7, 0.8 })
            for (float amp : { 0.15f, 0.25f })
            {
                const auto abC = barsOver (bassMidi, { 60, 63, 68 }, rho, amp, 900);
                const auto cm = barsOver (bassMidi, { 60, 63, 67 }, rho, amp, 901);
                const auto c = barsOver (bassMidi, { 55, 60, 64 }, rho, amp, 902);
                REQUIRE (abC.segments.size() >= 1);
                REQUIRE (cm.segments.size() >= 1);
                REQUIRE (c.segments.size() >= 1);
                const auto& a = abC.segments.front().chord;
                INFO ("bass " << bassMidi << " rho " << rho << " amp " << amp << ": " << sym (a) << ", "
                      << sym (cm.segments.front().chord) << ", " << sym (c.segments.front().chord));
                CHECK (a.root.pc() == 8);
                CHECK (a.bassPc() == 0);
                CHECK (sym (cm.segments.front().chord) == "Cm");
                CHECK (sym (c.segments.front().chord) == "C");
                ok += sym (a) == "Ab/C" ? 1 : 0;
                ++total;
            }
    std::cout << "[analysis] bright-bass Ab/C exact " << ok << "/" << total << "\n";
    CHECK (ok >= total - 2);
}
