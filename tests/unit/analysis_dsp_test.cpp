#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "analysis_synth.h"
#include "core/analysis/BeatAggregator.h"
#include "core/analysis/ChromaExtractor.h"
#include "core/analysis/FFT.h"
#include "core/analysis/Resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace so::analysis;

namespace
{
    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i)
            s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (to - from));
    }

    std::vector<float> sine (double hz, double sr, double seconds, float amp)
    {
        std::vector<float> v ((size_t) (sr * seconds));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = amp * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / sr);
        return v;
    }
}

TEST_CASE ("FFT matches a direct DFT and inverts", "[analysis][fft]")
{
    const FFT fft (6);
    REQUIRE (fft.size() == 64);

    std::mt19937 rng (7);
    std::vector<FFT::Complex> x (64), X;
    for (auto& v : x)
        v = { (float) rng() / 4294967296.0f - 0.5f, (float) rng() / 4294967296.0f - 0.5f };
    X = x;
    fft.forward (X.data());

    double maxErr = 0.0;
    for (int k = 0; k < 64; ++k)
    {
        std::complex<double> acc;
        for (int n = 0; n < 64; ++n)
            acc += std::complex<double> (x[(size_t) n]) * std::polar (1.0, -2.0 * std::numbers::pi * k * n / 64.0);
        maxErr = std::max (maxErr, std::abs (acc - std::complex<double> (X[(size_t) k])));
    }
    CHECK (maxErr < 1e-4);

    fft.inverse (X.data());
    double rtErr = 0.0;
    for (size_t i = 0; i < 64; ++i)
        rtErr = std::max (rtErr, (double) std::abs (X[i] - x[i]));
    CHECK (rtErr < 1e-5);

    CHECK_THROWS (FFT (0));
    CHECK (FFT::orderFor (1000) == 10);
}

TEST_CASE ("Resampler passes 1 kHz and rejects 15 kHz at 44.1k to 22.05k", "[analysis][resample]")
{
    const double sr = 44100.0;
    const auto pass = resample (sine (1000.0, sr, 1.0, 0.5f), sr, 22050.0);
    const auto stop = resample (sine (15000.0, sr, 1.0, 0.5f), sr, 22050.0);

    REQUIRE (pass.size() == 22050);
    REQUIRE (stop.size() == 22050);

    const double inRms = 0.5 / std::sqrt (2.0);
    const double passRms = rms (pass, 2000, 20000);
    const double stopRms = rms (stop, 2000, 20000);
    CHECK (passRms == Catch::Approx (inRms).epsilon (0.01));
    CHECK (20.0 * std::log10 (stopRms / inRms) < -60.0);

    // Upsampling keeps the tone too.
    const auto up = resample (sine (440.0, 16000.0, 0.5, 0.5f), 16000.0, 22050.0);
    CHECK (rms (up, 1000, up.size() - 1000) == Catch::Approx (inRms).epsilon (0.01));

    // Same rate is a copy; empty stays empty.
    const auto same = resample ({ 0.1f, 0.2f }, 22050.0, 22050.0);
    CHECK (same == std::vector<float> { 0.1f, 0.2f });
    CHECK (resample ({}, 44100.0, 22050.0).empty());
}

TEST_CASE ("Downmix averages channels", "[analysis][resample]")
{
    const auto m = downmix (std::vector<std::vector<float>> { { 1.0f, 0.0f, 0.5f }, { 0.0f, 1.0f } });
    REQUIRE (m.size() == 2);
    CHECK (m[0] == Catch::Approx (0.5f));
    CHECK (m[1] == Catch::Approx (0.5f));
}

TEST_CASE ("Chroma of a C major triad over an E bass", "[analysis][chroma]")
{
    test::Synth s (2.0, 22050.0, 11);
    s.chord (40, { 60, 64, 67 }, 0.0, 2.0);
    const auto frames = ChromaExtractor (ChromaSettings::forQuality (AnalysisQuality::Balanced))
                            .process (s.buf, 22050.0);
    REQUIRE (frames.size() > 10);

    BeatGrid g { 60.0, 4, 0.0 };
    const auto beats = aggregateBeats (frames, g, 2);
    REQUIRE (beats.size() == 2);
    CHECK_FALSE (beats[0].silent);

    const auto up = normalizeChroma (beats[0].chroma, 0.0f);
    const auto lo = normalizeChroma (beats[0].bass, 0.0f);

    // C, E, G dominate the upper chroma. B is the E2 bass's 3rd partial (B3): suppression
    // removes most of it, the rest survives because partials and notes add with random phase.
    for (int pc : { 0, 4, 7 })
        CHECK (up[(size_t) pc] > 0.6f);
    CHECK (up[11] < 0.5f);
    for (int pc : { 1, 2, 3, 5, 6, 8, 9, 10 })
        CHECK (up[(size_t) pc] < 0.15f);

    // Bass chroma peaks on E.
    CHECK (std::max_element (lo.begin(), lo.end()) - lo.begin() == 4);
}
