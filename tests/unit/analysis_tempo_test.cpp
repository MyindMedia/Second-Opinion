#include <catch2/catch_test_macros.hpp>

#include "analysis_synth.h"
#include "core/analysis/TempoEstimator.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

using namespace so::analysis;

namespace
{
    /** Chord stabs on every beat with a louder low thump on beat 1 of each bar, plus a little noise. */
    AudioClip groove (double bpm, double offsetSec, double seconds, std::uint32_t seed)
    {
        AudioClip c;
        c.sampleRate = 44100.0;
        c.samples.assign ((size_t) (seconds * c.sampleRate), 0.0f);
        std::mt19937 rng (seed);
        std::normal_distribution<float> noise (0.0f, 0.002f);
        for (auto& s : c.samples)
            s = noise (rng);

        const double beat = 60.0 / bpm;
        int n = 0;
        for (double t = offsetSec; t < seconds; t += beat, ++n)
        {
            const bool downbeat = n % 4 == 0;
            const size_t start = (size_t) (t * c.sampleRate);
            for (size_t i = 0; i < (size_t) (0.25 * c.sampleRate) && start + i < c.samples.size(); ++i)
            {
                const double tt = (double) i / c.sampleRate;
                const double env = std::exp (-tt * 14.0);
                double v = 0.0;
                for (double f : { 349.2, 415.3, 523.3 })   // F Ab C
                    v += 0.12 * std::sin (2.0 * std::numbers::pi * f * tt);
                if (downbeat)
                    v += 0.6 * std::sin (2.0 * std::numbers::pi * 55.0 * tt) * std::exp (-tt * 9.0);
                c.samples[start + i] += (float) (v * env);
            }
        }
        return c;
    }
}

TEST_CASE ("Tempo is detected for common production tempos", "[analysis][tempo]")
{
    for (double bpm : { 72.0, 92.0, 100.0, 128.0, 140.0 })
    {
        const auto est = estimateTempo (groove (bpm, 0.0, 20.0, 11));
        REQUIRE (est.has_value());
        INFO ("expected " << bpm << " got " << est->bpm);
        CHECK (std::abs (est->bpm - bpm) <= 1.0);
    }
}

TEST_CASE ("Downbeat phase follows the strong beat", "[analysis][tempo]")
{
    const double bpm = 92.0, offset = 0.37;
    const auto est = estimateTempo (groove (bpm, offset, 20.0, 5));
    REQUIRE (est.has_value());
    // Downbeat lands on a bar start: offset + k * bar, within 30 ms.
    const double bar = 4.0 * 60.0 / bpm;
    const double k = std::round ((est->downbeatSec - offset) / bar);
    CHECK (std::abs (est->downbeatSec - (offset + k * bar)) <= 0.03);
}

TEST_CASE ("File grid prefers the host tempo when the audio agrees", "[analysis][tempo]")
{
    const auto clip = groove (92.0, 0.0, 16.0, 3);
    CHECK (chooseFileGrid (clip, 92.0).bpm == 92.0);
    CHECK (chooseFileGrid (clip, 184.0).bpm == 184.0);          // double time agrees
    CHECK (std::abs (chooseFileGrid (clip, 120.0).bpm - 92.0) <= 1.0);   // disagrees: detected wins
    CHECK (std::abs (chooseFileGrid (clip, std::nullopt).bpm - 92.0) <= 1.0);
}

TEST_CASE ("Silence and short clips give no estimate", "[analysis][tempo]")
{
    AudioClip silent;
    silent.sampleRate = 44100.0;
    silent.samples.assign (44100 * 10, 0.0f);
    CHECK_FALSE (estimateTempo (silent).has_value());
    CHECK_FALSE (estimateTempo (groove (100.0, 0.0, 2.0, 1)).has_value());
}

TEST_CASE ("A decaying tail after the last bar is trimmed", "[analysis][tempo]")
{
    auto clip = groove (92.0, 0.0, 4 * 4 * 60.0 / 92.0 + 1.0, 9);   // 4 bars plus 1 s of near-silence
    const double bar = 4 * 60.0 / 92.0;
    BeatGrid grid;
    grid.bpm = 92.0;
    trimTrailingTail (clip, grid);
    CHECK (std::abs ((double) clip.samples.size() / clip.sampleRate - 4.0 * bar) < 0.01);
}

namespace
{
    /** Pop groove: chords change on every bar, kick on 1 and 3, a louder noise snare on 2 and 4
        (which used to pull the bar phase onto beat 2), audio starting right on the downbeat. */
    AudioClip backbeat (double bpm, double offsetSec, int bars, std::uint32_t seed)
    {
        const double beat = 60.0 / bpm;
        test::Synth s (offsetSec + bars * 4 * beat + 0.5, 44100.0, seed);
        const int roots[4] { 48, 53, 55, 45 };
        const int thirds[4] { 4, 4, 4, 3 };
        for (int b = 0; b < bars; ++b)
        {
            const double t0 = offsetSec + b * 4 * beat;
            const int r = roots[b % 4];
            for (int k = 0; k < 4; ++k)
                s.chord (r - 12, { r + 12, r + 12 + thirds[b % 4], r + 19 }, t0 + k * beat, beat * 0.9, 0.08f, 0.05f);
            for (int k = 0; k < 4; ++k)
            {
                const double tk = t0 + k * beat;
                if (k % 2 == 0)
                    s.tone (28, tk, 0.2, 0.5f, 1);   // kick-like low thump
                else
                    for (size_t i = (size_t) (tk * s.sr); i < (size_t) ((tk + 0.12) * s.sr) && i < s.buf.size(); ++i)
                        s.buf[i] += (float) ((s.uniform() * 2.0 - 1.0) * 0.5 * std::exp (-((double) i / s.sr - tk) * 30.0));
            }
        }
        s.noise (0.002f);
        return s.clip();
    }
}

TEST_CASE ("Downbeat follows chord changes over a backbeat snare", "[analysis][tempo]")
{
    for (double bpm : { 84.0, 96.0, 120.0 })
    {
        const auto clip = backbeat (bpm, 0.0, 8, 21);
        const auto grid = chooseFileGrid (clip, bpm);
        INFO ("bpm " << bpm << " downbeat " << grid.downbeatSec);
        CHECK (grid.bpm == bpm);
        CHECK (grid.downbeatSec <= 0.03);   // bar 1 starts at 0, not a beat (or a half beat) later
    }

    // With a pickup: the first full bar starts 0.4 s in.
    const auto pickup = backbeat (100.0, 0.4, 8, 22);
    CHECK (std::abs (chooseFileGrid (pickup, 100.0).downbeatSec - 0.4) <= 0.03);

    // A double-time host tempo: the phase is recomputed for the tempo actually used.
    const auto grid = chooseFileGrid (backbeat (90.0, 0.0, 8, 23), 180.0);
    CHECK (grid.bpm == 180.0);
    CHECK (grid.downbeatSec <= 0.03);
}
