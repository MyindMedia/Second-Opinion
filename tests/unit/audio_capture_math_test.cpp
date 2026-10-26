#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/audio/CaptureMath.h"

using namespace so::audio;
using Catch::Approx;

namespace
{
    CaptureStamp stampAt (double ppq, double bpm = 120.0, int num = 4, int den = 4)
    {
        CaptureStamp s;
        s.hasTempo = true;
        s.hasPpq = true;
        s.bpm = bpm;
        s.firstSamplePpq = ppq;
        s.numerator = num;
        s.denominator = den;
        s.sampleRate = 48000.0;
        return s;
    }
}

TEST_CASE ("Capture that starts on a bar line has its downbeat at 0", "[audio][downbeat]")
{
    const auto a = alignToBars (stampAt (8.0));
    CHECK (a.grid.downbeatSec == Approx (0.0).margin (1e-12));
    CHECK (a.bar1Ppq == Approx (8.0));
    CHECK (a.grid.bpm == 120.0);
    CHECK (a.grid.beatsPerBar == 4);
}

TEST_CASE ("Mid-bar capture: beat 0 is the next bar line", "[audio][downbeat]")
{
    // Bar 3 spans PPQ 8..12; starting at 10.3 the next bar line is 12, 1.7 quarters later.
    const auto a = alignToBars (stampAt (10.3));
    CHECK (a.bar1Ppq == Approx (12.0));
    CHECK (a.grid.downbeatSec == Approx (1.7 * 0.5));
    CHECK (a.grid.beatToSec (0.0) * 48000.0 == Approx (40800.0).margin (1.0));
}

TEST_CASE ("A start within one sample after a bar line keeps that bar", "[audio][downbeat]")
{
    const double halfSampleQn = 0.5 * (120.0 / 60.0 / 48000.0);
    const auto a = alignToBars (stampAt (8.0 + halfSampleQn));
    CHECK (a.bar1Ppq == Approx (8.0));
    CHECK (a.grid.downbeatSec * 48000.0 == Approx (-0.5).margin (1e-6));
}

TEST_CASE ("Host bar start and odd meters are honoured", "[audio][downbeat]")
{
    auto s = stampAt (7.0, 90.0, 3, 4);
    s.hasLastBarStart = true;
    s.lastBarStartPpq = 6.0;            // meter change put bar lines at 6, 9, 12...
    const auto a = alignToBars (s);
    CHECK (a.bar1Ppq == Approx (9.0));
    CHECK (a.grid.downbeatSec == Approx (2.0 * 60.0 / 90.0));
    CHECK (a.grid.beatsPerBar == 3);

    CHECK (barLengthQuarters (6, 8) == 3.0);
    CHECK (alignToBars (stampAt (0.0, 120.0, 6, 8)).grid.beatsPerBar == 3);
}

TEST_CASE ("Negative PPQ (pre-roll) aligns to bar 1 at PPQ 0", "[audio][downbeat]")
{
    const auto a = alignToBars (stampAt (-0.5));
    CHECK (a.bar1Ppq == Approx (0.0));
    CHECK (a.grid.downbeatSec == Approx (0.25));
}

TEST_CASE ("Without host tempo the downbeat is the capture start (AC5)", "[audio][downbeat]")
{
    CaptureStamp s;
    s.firstSamplePpq = 10.3;
    const auto a = alignToBars (s, 97.0);
    CHECK (a.grid.bpm == 97.0);
    CHECK (a.grid.downbeatSec == 0.0);
}

TEST_CASE ("Capture limits: 64 bars capped at 3 minutes", "[audio][limit]")
{
    CHECK (captureLimitFrames (stampAt (0.0, 120.0)) == (std::size_t) (128.0 * 48000.0));
    CHECK (captureLimitFrames (stampAt (0.0, 60.0)) == (std::size_t) (180.0 * 48000.0));
    CaptureStamp noTempo;
    noTempo.sampleRate = 44100.0;
    CHECK (captureLimitFrames (noTempo) == (std::size_t) (180.0 * 44100.0));
    CHECK (captureCapacityFrames (48000.0, 120.0, 4, 4) == (std::size_t) (128.0 * 48000.0));
    CHECK (captureCapacityFrames (48000.0, 0.0, 4, 4) == (std::size_t) (180.0 * 48000.0));
}

TEST_CASE ("Mono downmix averages channels", "[audio][downmix]")
{
    const float l[3] { 1.0f, 0.5f, -1.0f };
    const float r[3] { 0.0f, 0.5f, 1.0f };
    const float* ch[2] { l, r };
    const auto clip = downmixToMono (ch, 2, 3, 44100.0);
    REQUIRE (clip.samples.size() == 3);
    CHECK (clip.samples[0] == 0.5f);
    CHECK (clip.samples[1] == 0.5f);
    CHECK (clip.samples[2] == 0.0f);
    CHECK (clip.sampleRate == 44100.0);
}
