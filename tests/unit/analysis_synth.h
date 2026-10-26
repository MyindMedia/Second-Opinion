#pragma once

// Deterministic test-signal synthesis for the analysis tests (explicit seeds, no
// implementation-defined distributions so results match on every platform).

#include "core/analysis/AnalysisTypes.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

namespace so::analysis::test
{

inline double midiHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

class Synth
{
public:
    Synth (double seconds, double sampleRate, std::uint32_t seed)
        : sr (sampleRate), buf ((size_t) (seconds * sampleRate), 0.0f), rng (seed) {}

    /** A note with `harmonics` partials at 1/h amplitude, 10 ms attack, gentle decay, 30 ms release. */
    void tone (int midi, double start, double dur, float amp, int harmonics = 4)
    {
        const double f0 = midiHz (midi) * std::pow (2.0, tuneCents / 1200.0);
        std::vector<double> phase ((size_t) harmonics);
        for (auto& p : phase)
            p = uniform() * 2.0 * std::numbers::pi;

        const auto s0 = (long long) (start * sr);
        const auto n = (long long) (dur * sr);
        const double att = 0.010 * sr, rel = 0.030 * sr;
        for (long long i = 0; i < n; ++i)
        {
            const long long j = s0 + i;
            if (j < 0 || j >= (long long) buf.size())
                continue;
            double env = std::exp (-0.6 * (double) i / sr);
            if ((double) i < att) env *= (double) i / att;
            if ((double) (n - i) < rel) env *= (double) (n - i) / rel;

            double v = 0.0;
            for (int h = 1; h <= harmonics; ++h)
            {
                const double fh = f0 * h;
                if (fh >= sr * 0.45)
                    break;
                v += std::sin (2.0 * std::numbers::pi * fh * (double) i / sr + phase[(size_t) h - 1]) / h;
            }
            buf[(size_t) j] += (float) (amp * env * v);
        }
    }

    void chord (int bassMidi, const std::vector<int>& upper, double start, double dur,
                float bassAmp = 0.15f, float upperAmp = 0.08f)
    {
        if (bassMidi > 0)
            tone (bassMidi, start, dur, bassAmp, 5);
        for (int m : upper)
            tone (m, start, dur, upperAmp, 4);
    }

    void noise (float amp)
    {
        for (auto& s : buf)
            s += (float) ((uniform() * 2.0 - 1.0) * amp);
    }

    double uniform() { return (double) rng() / 4294967296.0; }

    AudioClip clip() const { return { buf, sr }; }

    double sr;
    std::vector<float> buf;
    double tuneCents { 0.0 };   // global detune applied to every tone

private:
    std::mt19937 rng;
};

/** Chord of the segment covering `beat` (nullptr when uncovered). */
inline const ChordSegment* segmentAt (const AnalysisResult& r, double beat)
{
    for (const auto& s : r.segments)
        if (beat >= s.startBeat && beat < s.startBeat + s.beats)
            return &s;
    return nullptr;
}

} // namespace so::analysis::test
