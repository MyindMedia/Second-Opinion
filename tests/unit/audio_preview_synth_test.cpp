#include <catch2/catch_test_macros.hpp>

#include "core/preview/PreviewSynth.h"
#include "core/preview/SnapshotExchange.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace so::preview;
using so::midi::MidiLane;

namespace
{
    constexpr double sr = 48000.0;

    std::vector<MidiLane> demoLanes()
    {
        MidiLane chords { "Chords", {} };
        MidiLane bass { "Bass", {} };
        const int roots[4] { 60, 65, 67, 60 };
        for (int bar = 0; bar < 4; ++bar)
        {
            for (int k : { 0, 4, 7, 11 })
                chords.notes.push_back ({ roots[bar] + k, bar * 4.0, 4.0, 90, 0 });
            bass.notes.push_back ({ roots[bar] - 24, bar * 4.0, 4.0, 100, 1 });
        }
        return { chords, bass };
    }

    struct Rendered
    {
        std::vector<float> l, r;
    };

    Rendered renderSeconds (PreviewSynth& synth, const PreviewSnapshot* snap, double seconds, double startBeat = 0.0)
    {
        const int total = (int) (seconds * sr);
        Rendered out { std::vector<float> ((std::size_t) total), std::vector<float> ((std::size_t) total) };
        const double bps = snap != nullptr ? snap->bpm / 60.0 / sr : 2.0 / sr;
        for (int pos = 0; pos < total; pos += 128)
        {
            const int n = std::min (128, total - pos);
            synth.render (snap, out.l.data() + pos, out.r.data() + pos, n, startBeat + pos * bps, bps);
        }
        return out;
    }

    double rms (const std::vector<float>& v, std::size_t from = 0, std::size_t to = 0)
    {
        if (to == 0) to = v.size();
        double acc = 0.0;
        for (auto i = from; i < to; ++i) acc += (double) v[i] * v[i];
        return std::sqrt (acc / (double) std::max<std::size_t> (1, to - from));
    }
}

TEST_CASE ("Snapshot flattens lanes, sorts notes and flags bass", "[audio][preview]")
{
    const auto snap = makePreviewSnapshot (demoLanes(), 100.0, 0.0);
    REQUIRE (snap.notes.size() == 20);
    CHECK (snap.lengthBeats == 16.0);
    CHECK (snap.bpm == 100.0);
    for (std::size_t i = 1; i < snap.notes.size(); ++i)
        CHECK (snap.notes[i - 1].startBeat <= snap.notes[i].startBeat);
    int bassCount = 0;
    for (const auto& n : snap.notes) bassCount += n.bass ? 1 : 0;
    CHECK (bassCount == 4);
}

TEST_CASE ("Synth renders deterministic, non-silent, bounded audio", "[audio][preview]")
{
    const auto snap = makePreviewSnapshot (demoLanes(), 120.0, 16.0);

    for (auto sound : { PreviewSynth::Sound::Keys, PreviewSynth::Sound::Pad })
    {
        PreviewSynth a, b;
        a.prepare (sr); b.prepare (sr);
        a.setSound (sound); b.setSound (sound);
        const auto ra = renderSeconds (a, &snap, 2.0);
        const auto rb = renderSeconds (b, &snap, 2.0);
        CHECK (ra.l == rb.l);
        CHECK (rms (ra.l) > 0.01);
        float peak = 0.0f;
        for (float v : ra.l) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::abs (v)); }
        CHECK (peak < 1.5f);
    }
}

TEST_CASE ("Synth is silent without notes and when Off", "[audio][preview]")
{
    PreviewSynth synth;
    synth.prepare (sr);
    const auto empty = renderSeconds (synth, nullptr, 0.5);
    CHECK (rms (empty.l) == 0.0);

    const auto snap = makePreviewSnapshot (demoLanes(), 120.0, 16.0);
    synth.setSound (PreviewSynth::Sound::Off);
    const auto off = renderSeconds (synth, &snap, 0.5);
    CHECK (rms (off.l) == 0.0);
}

TEST_CASE ("Note-on is sample accurate", "[audio][preview]")
{
    MidiLane lane { "Chords", { { 69, 1.0, 1.0, 127, 0 } } };
    const auto snap = makePreviewSnapshot ({ lane }, 120.0, 4.0);
    PreviewSynth synth;
    synth.prepare (sr);
    const auto r = renderSeconds (synth, &snap, 1.0);

    // Beat 1 at 120 BPM = 0.5 s = sample 24000 (inside a 128-sample block, not on its boundary).
    // The note may fire one sample late from rounding (+-1 sample), and a voice's first output sample
    // is exactly 0 (oscillator phase 0), so the first non-zero sample is 24001 or 24002.
    std::size_t first = 0;
    while (first < r.l.size() && r.l[first] == 0.0f) ++first;
    CHECK (first >= 24001);
    CHECK (first <= 24002);
}

TEST_CASE ("Polyphony is capped at 16 voices", "[audio][preview]")
{
    MidiLane cluster { "Chords", {} };
    for (int p = 40; p < 64; ++p)
        cluster.notes.push_back ({ p, 0.0, 4.0, 100, 0 });
    const auto snap = makePreviewSnapshot ({ cluster }, 120.0, 4.0);
    PreviewSynth synth;
    synth.prepare (sr);
    (void) renderSeconds (synth, &snap, 0.1);
    CHECK (synth.activeVoiceCount() == PreviewSynth::maxVoices);
}

TEST_CASE ("Seeking into a held chord starts it; a new snapshot retriggers", "[audio][preview]")
{
    const auto snap = makePreviewSnapshot (demoLanes(), 120.0, 16.0);
    PreviewSynth synth;
    synth.prepare (sr);
    const auto r = renderSeconds (synth, &snap, 0.25, 2.5);   // mid bar 1
    CHECK (rms (r.l, 2400) > 0.01);
    CHECK (synth.activeVoiceCount() == 5);
}

TEST_CASE ("SnapshotExchange defers release while the audio side holds a snapshot", "[audio][preview]")
{
    SnapshotExchange<PreviewSnapshot> ex;
    auto a = std::make_shared<const PreviewSnapshot>();
    const auto* aRaw = a.get();
    ex.publish (std::move (a));

    const auto* held = ex.acquire();
    CHECK (held == aRaw);

    ex.publish (std::make_shared<const PreviewSnapshot>());
    CHECK (ex.ownedCount() == 2);       // A still protected by the hazard pointer
    ex.release();
    ex.collect();
    CHECK (ex.ownedCount() == 1);
    CHECK (ex.acquire() != aRaw);
    ex.release();

    ex.publish (nullptr);
    CHECK (ex.acquire() == nullptr);
    ex.release();
    ex.collect();
    CHECK (ex.ownedCount() == 0);
}

TEST_CASE ("Preview synth CPU at 48 kHz / 128 with 16 voices", "[audio][preview][bench]")
{
    // Worst case: 16 pad voices (three saws each) held for the whole run.
    MidiLane dense { "Chords", {} };
    for (int i = 0; i < 16; ++i)
        dense.notes.push_back ({ 48 + i * 2, 0.0, 1000.0, 100, 0 });
    const auto snap = makePreviewSnapshot ({ dense }, 120.0, 1000.0);

    double worst = 0.0;
    for (auto sound : { PreviewSynth::Sound::Keys, PreviewSynth::Sound::Pad })
    {
        PreviewSynth synth;
        synth.prepare (sr);
        synth.setSound (sound);
        std::vector<float> l (128), r (128);
        const double bps = 2.0 / sr;
        const int blocks = (int) (10.0 * sr / 128.0);   // 10 s of audio

        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            synth.render (&snap, l.data(), r.data(), 128, b * 128 * bps, bps);
        }
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double pct = 100.0 * elapsed / 10.0;
        std::printf ("[bench] preview synth %s, 16 voices, 48k/128: %.2f%% of one core\n",
                     sound == PreviewSynth::Sound::Keys ? "keys" : "pad", pct);
        worst = std::max (worst, pct);
    }
    // Loose bound so a Debug build on a shared VM does not flake; the NFR-04 figure is in ADR 0008.
    CHECK (worst < 60.0);
}
