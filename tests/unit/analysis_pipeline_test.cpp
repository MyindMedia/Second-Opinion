#include <catch2/catch_test_macros.hpp>

#include "analysis_synth.h"
#include "core/analysis/Analyzer.h"
#include "core/theory/ChordNamer.h"

#include <chrono>
#include <iostream>

using namespace so::analysis;
using namespace so::theory;

namespace
{
    // I - vi - IV - V in G with a simple melody on top, `bars` long.
    AudioClip song (int bars, const BeatGrid& g, std::uint32_t seed)
    {
        const double beat = 60.0 / g.bpm;
        const double barSec = beat * g.beatsPerBar;
        test::Synth s (g.downbeatSec + barSec * bars + 0.2, 44100.0, seed);

        struct B { int bass; std::vector<int> up; };
        const B prog[4] { { 43, { 55, 59, 62 } }, { 40, { 55, 59, 64 } }, { 36, { 55, 60, 64 } }, { 38, { 54, 57, 62 } } };
        const int mel[4][4] { { 71, 74, 79, 74 }, { 76, 71, 67, 71 }, { 72, 76, 79, 76 }, { 74, 78, 81, 78 } };

        for (int b = 0; b < bars; ++b)
        {
            const double t = g.downbeatSec + barSec * b;
            s.chord (prog[b % 4].bass, prog[b % 4].up, t, barSec);
            for (int k = 0; k < 4; ++k)
                s.tone (mel[b % 4][k], t + beat * k, beat * 0.9, 0.12f, 4);
        }
        s.noise (0.002f);
        return s.clip();
    }

    bool sameResult (const AnalysisResult& a, const AnalysisResult& b)
    {
        if (a.bars != b.bars || ! (a.key == b.key) || a.keyConfidence != b.keyConfidence
            || a.segments.size() != b.segments.size() || a.notes.size() != b.notes.size()
            || a.melody.size() != b.melody.size())
            return false;
        for (size_t i = 0; i < a.segments.size(); ++i)
        {
            const auto& x = a.segments[i];
            const auto& y = b.segments[i];
            if (x.id != y.id || x.startBeat != y.startBeat || x.beats != y.beats || ! (x.chord == y.chord)
                || x.confidence != y.confidence || x.alternatives.size() != y.alternatives.size())
                return false;
            for (size_t k = 0; k < x.alternatives.size(); ++k)
                if (! (x.alternatives[k].chord == y.alternatives[k].chord)
                    || x.alternatives[k].confidence != y.alternatives[k].confidence)
                    return false;
        }
        for (size_t i = 0; i < a.notes.size(); ++i)
            if (a.notes[i].pitch != b.notes[i].pitch || a.notes[i].startSec != b.notes[i].startSec
                || a.notes[i].endSec != b.notes[i].endSec)
                return false;
        return true;
    }
}

TEST_CASE ("Pipeline: silence does not crash and yields nothing", "[analysis][pipeline]")
{
    const BeatGrid g { 120.0, 4, 0.0 };
    AnalysisStatus st;
    const auto r = analyze ({ std::vector<float> (44100 * 4, 0.0f), 44100.0 }, g, {}, {}, nullptr, &st);
    CHECK (r.bars == 2);
    CHECK (r.segments.empty());
    CHECK (r.notes.empty());
    CHECK (r.melody.empty());
    CHECK_FALSE (st.cancelled);
    CHECK_FALSE (st.message.empty());

    const auto empty = analyze ({ {}, 44100.0 }, g, {}, {}, nullptr, &st);
    CHECK (empty.bars == 0);
    CHECK_FALSE (st.message.empty());

    const auto badGrid = analyze ({ std::vector<float> (100, 0.1f), 44100.0 }, { 0.0, 4, 0.0 }, {}, {}, nullptr, &st);
    CHECK (badGrid.bars == 0);
}

TEST_CASE ("Pipeline: progress runs through the three stages in order", "[analysis][pipeline]")
{
    const BeatGrid g { 120.0, 4, 0.0 };
    std::vector<std::pair<int, float>> calls;
    const auto r = analyze (song (2, g, 3), g, {}, [&] (int s, float p) { calls.emplace_back (s, p); });

    REQUIRE_FALSE (calls.empty());
    CHECK (calls.front() == std::pair<int, float> { 0, 0.0f });
    CHECK (calls.back() == std::pair<int, float> { 2, 1.0f });
    for (size_t i = 1; i < calls.size(); ++i)
    {
        CHECK (calls[i].first >= calls[i - 1].first);
        if (calls[i].first == calls[i - 1].first)
            CHECK (calls[i].second >= calls[i - 1].second);
        CHECK (calls[i].second >= 0.0f);
        CHECK (calls[i].second <= 1.0f);
    }
    CHECK (r.bars == 2);
}

TEST_CASE ("Pipeline: cancel before start and mid-run", "[analysis][pipeline]")
{
    const BeatGrid g { 120.0, 4, 0.0 };
    const auto clip = song (8, g, 4);

    std::atomic<bool> cancel { true };
    AnalysisStatus st;
    auto r = analyze (clip, g, {}, {}, &cancel, &st);
    CHECK (st.cancelled);
    CHECK (r.segments.empty());

    // Cancel as soon as harmony reading starts.
    cancel = false;
    int lastStage = -1;
    r = analyze (clip, g, {}, [&] (int s, float p)
    {
        lastStage = s;
        if (s == (int) AnalysisStage::ReadingHarmony && p > 0.1f)
            cancel = true;
    }, &cancel, &st);
    CHECK (st.cancelled);
    CHECK (r.segments.empty());
    CHECK (lastStage == (int) AnalysisStage::ReadingHarmony);

    // Cancel while transcribing: no harmony stage reported afterwards.
    cancel = false;
    lastStage = -1;
    r = analyze (clip, g, {}, [&] (int s, float p)
    {
        lastStage = s;
        if (s == (int) AnalysisStage::TranscribingNotes && p > 0.2f)
            cancel = true;
    }, &cancel, &st);
    CHECK (st.cancelled);
    CHECK (lastStage == (int) AnalysisStage::TranscribingNotes);
}

TEST_CASE ("Pipeline: deterministic, same input gives the identical result", "[analysis][pipeline]")
{
    const BeatGrid g { 96.0, 4, 0.1 };
    const auto clip = song (4, g, 77);
    const auto a = analyze (clip, g);
    const auto b = analyze (clip, g);
    REQUIRE_FALSE (a.segments.empty());
    CHECK (sameResult (a, b));
}

TEST_CASE ("Pipeline: song with melody, key override and external notes", "[analysis][pipeline]")
{
    const BeatGrid g { 110.0, 4, 0.0 };
    const auto clip = song (4, g, 8);
    const NamingOptions ascii { NamingOptions::Style::Pop, true, NamingOptions::Spelling::Auto, AccidentalStyle::Ascii };

    const auto r = analyze (clip, g);
    std::cout << "[analysis] G song: key " << r.key.toString (AccidentalStyle::Ascii) << ", chords";
    for (const auto& s : r.segments)
        std::cout << " " << ChordNamer::name (s.chord, ascii) << "@" << s.startBeat;
    std::cout << ", melody notes " << r.melody.size() << "\n";

    CHECK (r.key.tonic.pc() == 7);
    CHECK (r.key.mode == Mode::Major);
    const char* want[4] { "G", "Em", "C", "D" };
    for (int bar = 0; bar < 4; ++bar)
    {
        const auto* seg = test::segmentAt (r, bar * 4 + 2.0);
        REQUIRE (seg != nullptr);
        CHECK (ChordNamer::name (seg->chord, ascii) == want[bar]);
    }
    CHECK (r.melody.size() >= 8);   // 16 melody notes over the chords; YIN on a full mix is best effort

    AnalyzerOptions o;
    o.keyOverride = Key::parse ("E minor");
    const auto ro = analyze (clip, g, o);
    CHECK (ro.key.toString (AccidentalStyle::Ascii) == "E minor");
    CHECK (ro.keyConfidence == 1.0f);
    const auto* d = test::segmentAt (ro, 14.0);
    REQUIRE (d != nullptr);
    CHECK (ChordNamer::name (d->chord, ascii) == "D");

    // External (Basic Pitch style) notes are used as the transcription.
    AnalyzerOptions on;
    on.notes = std::vector<NoteEvent> { { 79, 0.0, 0.5, 0.9f, 0.9f }, { 43, 0.0, 2.0, 0.9f, 0.9f } };
    const auto rn = analyze (clip, g, on);
    CHECK (rn.notes.size() == 2);
    REQUIRE (rn.melody.size() == 1);
    CHECK (rn.melody[0].pitch == 79);
}

TEST_CASE ("Pipeline: performance, 32 bars at 120 BPM", "[analysis][perf]")
{
    const BeatGrid g { 120.0, 4, 0.0 };
    const auto clip = song (32, g, 2024);

    const auto t0 = std::chrono::steady_clock::now();
    const auto r = analyze (clip, g);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    std::cout << "[analysis] perf: 64 s of audio (32 bars @ 120 BPM, 44.1 kHz) analysed in " << sec << " s, "
              << r.segments.size() << " segments, " << r.notes.size() << " notes\n";

    CHECK (r.bars == 32);
#ifdef NDEBUG
    CHECK (sec < 10.0);
#endif
}
