#include "KeyEstimator.h"

#include <algorithm>
#include <cmath>

namespace so::analysis
{

namespace
{
    constexpr std::array<double, 12> kMajor { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    constexpr std::array<double, 12> kMinor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    constexpr double kSoftmaxTemperature = 0.04;

    double pearson (const std::array<double, 12>& x, const std::array<double, 12>& profile, int tonic)
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            mx += x[(size_t) i];
            my += profile[(size_t) i];
        }
        mx /= 12.0;
        my /= 12.0;

        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = x[(size_t) theory::wrapPc (i + tonic)] - mx;
            const double b = profile[(size_t) i] - my;
            sxy += a * b;
            sxx += a * a;
            syy += b * b;
        }
        return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    }
}

theory::Key KeyEstimator::keyFor (int pc, theory::Mode mode)
{
    using theory::Letter;
    struct S { Letter l; int acc; };
    static constexpr S major[12] { { Letter::C, 0 }, { Letter::D, -1 }, { Letter::D, 0 }, { Letter::E, -1 },
                                   { Letter::E, 0 }, { Letter::F, 0 },  { Letter::F, 1 }, { Letter::G, 0 },
                                   { Letter::A, -1 }, { Letter::A, 0 }, { Letter::B, -1 }, { Letter::B, 0 } };
    static constexpr S minor[12] { { Letter::C, 0 }, { Letter::C, 1 },  { Letter::D, 0 }, { Letter::E, -1 },
                                   { Letter::E, 0 }, { Letter::F, 0 },  { Letter::F, 1 }, { Letter::G, 0 },
                                   { Letter::G, 1 }, { Letter::A, 0 },  { Letter::B, -1 }, { Letter::B, 0 } };
    const auto& s = (mode == theory::Mode::Major ? major : minor)[theory::wrapPc (pc)];
    return { { s.l, s.acc }, mode };
}

KeyEstimate KeyEstimator::estimate (const std::array<double, 12>& h)
{
    KeyEstimate e;
    e.key = keyFor (0, theory::Mode::Major);

    double total = 0.0;
    for (double v : h)
        total += v;
    if (total <= 0.0)
        return e;

    int best = 0;
    for (int k = 0; k < 24; ++k)
    {
        e.correlation[(size_t) k] = (float) pearson (h, k < 12 ? kMajor : kMinor, k % 12);
        if (e.correlation[(size_t) k] > e.correlation[(size_t) best])
            best = k;
    }

    double z = 0.0;
    for (float c : e.correlation)
        z += std::exp ((c - e.correlation[(size_t) best]) / kSoftmaxTemperature);

    e.key = keyFor (best % 12, best < 12 ? theory::Mode::Major : theory::Mode::Minor);
    e.confidence = (float) (1.0 / z);
    return e;
}

std::array<double, 12> KeyEstimator::histogram (const std::vector<BeatFeatures>& beats,
                                                const std::vector<ChordSegment>* segments)
{
    std::array<double, 12> h {};

    for (const auto& b : beats)
    {
        if (b.silent)
            continue;
        const auto up = normalizeChroma (b.chroma, 0.15f);
        const auto lo = normalizeChroma (b.bass, 0.2f);
        for (size_t i = 0; i < 12; ++i)
            h[i] += up[i] + 0.5 * lo[i];
    }

    if (segments != nullptr && ! segments->empty())
    {
        for (size_t i = 0; i < segments->size(); ++i)
        {
            const auto& s = (*segments)[i];
            double w = s.beats * 0.5;
            if (i == 0 || i + 1 == segments->size())
                w *= 1.5;
            for (int pc : s.chord.chordTonePcs())
                h[(size_t) pc] += w;
            h[(size_t) s.chord.root.pc()] += w;
            if (s.chord.isSlash())
                h[(size_t) s.chord.bassPc()] += 0.5 * w;
        }
    }

    return h;
}

} // namespace so::analysis
