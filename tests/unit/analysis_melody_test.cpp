#include <catch2/catch_test_macros.hpp>

#include "analysis_synth.h"
#include "core/analysis/MelodyTracker.h"
#include "core/analysis/Resampler.h"

#include <cmath>
#include <iostream>

using namespace so::analysis;

TEST_CASE ("Melody: 8-note line recovered with exact pitch and onsets", "[analysis][melody]")
{
    const std::vector<int> pitches { 72, 74, 76, 79, 81, 79, 76, 74 };
    const double start = 0.5, len = 0.35, gap = 0.05;

    test::Synth s (start + (len + gap) * 8 + 0.5, 44100.0, 42);
    for (size_t i = 0; i < pitches.size(); ++i)
        s.tone (pitches[i], start + (double) i * (len + gap), len, 0.3f, 5);
    s.noise (0.003f);

    const auto audio = resample (s.buf, 44100.0);
    const auto notes = MelodyTracker().track (audio, kAnalysisRate);

    std::cout << "[analysis] melody notes:";
    for (const auto& n : notes)
        std::cout << " " << n.pitch << "@" << n.startSec;
    std::cout << "\n";

    int exact = 0, onTime = 0;
    for (size_t i = 0; i < pitches.size(); ++i)
    {
        const double want = start + (double) i * (len + gap);
        const NoteEvent* best = nullptr;
        for (const auto& n : notes)
            if (best == nullptr || std::abs (n.startSec - want) < std::abs (best->startSec - want))
                best = &n;
        if (best == nullptr)
            continue;
        if (std::abs (best->startSec - want) <= 0.060)
        {
            ++onTime;
            if (best->pitch == pitches[i])
                ++exact;
        }
    }

    CHECK (notes.size() >= 7);
    CHECK (notes.size() <= 9);
    CHECK (exact >= 7);
    CHECK (onTime >= 7);
    for (const auto& n : notes)
    {
        CHECK (n.endSec - n.startSec >= 0.08);
        CHECK (n.confidence > 0.5f);
    }
}

TEST_CASE ("Melody: legato pitch changes split without gaps", "[analysis][melody]")
{
    test::Synth s (2.2, 22050.0, 5);
    s.tone (67, 0.2, 0.6, 0.3f, 4);
    s.tone (69, 0.8, 0.6, 0.3f, 4);
    s.tone (71, 1.4, 0.6, 0.3f, 4);
    const auto notes = MelodyTracker().track (s.buf, 22050.0);
    REQUIRE (notes.size() == 3);
    CHECK (notes[0].pitch == 67);
    CHECK (notes[1].pitch == 69);
    CHECK (notes[2].pitch == 71);
    CHECK (std::abs (notes[1].startSec - 0.8) < 0.06);
}

TEST_CASE ("Melody: silence and very short blips give no notes", "[analysis][melody]")
{
    const std::vector<float> silence (22050 * 3, 0.0f);
    CHECK (MelodyTracker().track (silence, 22050.0).empty());
    CHECK (MelodyTracker().track ({}, 22050.0).empty());

    test::Synth s (1.0, 22050.0, 9);
    s.tone (69, 0.4, 0.04, 0.3f);   // 40 ms: below the 80 ms minimum
    CHECK (MelodyTracker().track (s.buf, 22050.0).empty());
}

TEST_CASE ("Melody notes on the beat grid", "[analysis][melody]")
{
    const BeatGrid g { 120.0, 4, 0.0 };
    const std::vector<NoteEvent> ev { { 60, 0.01, 0.49 }, { 62, 0.5, 1.26 }, { 64, 9.0, 9.5 } };
    const auto m = toMelodyNotes (ev, g, 8.0);
    REQUIRE (m.size() == 2);   // the last one is past 8 beats
    CHECK (m[0].pitch == 60);
    CHECK (m[0].startBeat == 0.0);
    CHECK (m[0].beats == 1.0);
    CHECK (m[1].startBeat == 1.0);
    CHECK (m[1].beats == 1.5);
}

TEST_CASE ("Skyline keeps the top voice", "[analysis][melody]")
{
    const std::vector<NoteEvent> poly { { 48, 0.0, 2.0 }, { 60, 0.0, 1.0 }, { 72, 0.0, 1.0 }, { 67, 1.0, 2.0 } };
    const auto top = skyline (poly);
    REQUIRE (top.size() == 2);
    CHECK (top[0].pitch == 72);
    CHECK (top[1].pitch == 67);
}
