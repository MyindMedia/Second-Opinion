#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace so::analysis
{

namespace
{
    constexpr int kZeroCrossings = 24;
    constexpr int kTableRes = 512;       // table points per zero crossing
    constexpr double kKaiserBeta = 8.6;
    constexpr double kCutoffFraction = 0.94;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            const double t = x / (2.0 * k);
            term *= t * t;
            sum += term;
            if (term < 1e-14 * sum)
                break;
        }
        return sum;
    }

    /** sinc(u) * kaiser(u / Z) for u in [0, Z], sampled kTableRes times per unit. */
    const std::vector<float>& kernelTable()
    {
        static const std::vector<float> table = []
        {
            std::vector<float> t ((size_t) (kZeroCrossings * kTableRes + 2), 0.0f);
            const double i0b = besselI0 (kKaiserBeta);
            for (size_t i = 0; i < t.size(); ++i)
            {
                const double u = (double) i / kTableRes;
                if (u >= kZeroCrossings)
                    break;
                const double r = u / kZeroCrossings;
                const double win = besselI0 (kKaiserBeta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
                const double pu = std::numbers::pi * u;
                const double s = u < 1e-12 ? 1.0 : std::sin (pu) / pu;
                t[i] = (float) (s * win);
            }
            return t;
        }();
        return table;
    }
}

std::vector<float> downmix (const std::vector<std::vector<float>>& channels)
{
    if (channels.empty())
        return {};

    size_t len = channels.front().size();
    for (const auto& c : channels)
        len = std::min (len, c.size());

    std::vector<const float*> ptrs;
    for (const auto& c : channels)
        ptrs.push_back (c.data());

    return downmix (ptrs.data(), (int) ptrs.size(), len);
}

std::vector<float> downmix (const float* const* channels, int numChannels, size_t numSamples)
{
    std::vector<float> out (numSamples, 0.0f);
    if (numChannels <= 0)
        return out;

    const float g = 1.0f / (float) numChannels;
    for (int c = 0; c < numChannels; ++c)
        for (size_t i = 0; i < numSamples; ++i)
            out[i] += channels[c][i] * g;
    return out;
}

std::vector<float> resample (const std::vector<float>& in, double inRate, double outRate,
                             const std::atomic<bool>* cancel)
{
    if (in.empty() || inRate <= 0.0 || outRate <= 0.0)
        return {};

    if (std::abs (inRate - outRate) < 1e-9)
        return in;

    const double ratio = outRate / inRate;
    const double fc = std::min (1.0, ratio) * kCutoffFraction;   // cutoff relative to input Nyquist
    const double halfWidth = kZeroCrossings / fc;                // in input samples
    const int reach = (int) std::ceil (halfWidth);

    const auto& table = kernelTable();
    const double tableScale = fc * kTableRes;
    const auto numIn = (long long) in.size();
    const auto numOut = (size_t) std::floor ((double) in.size() * ratio);

    std::vector<float> out (numOut, 0.0f);

    for (size_t n = 0; n < numOut; ++n)
    {
        if (cancel != nullptr && (n & 8191) == 0 && cancel->load (std::memory_order_relaxed))
            return {};

        const double t = (double) n / ratio;
        const auto centre = (long long) std::floor (t);

        double acc = 0.0, wsum = 0.0;
        for (long long j = centre - reach + 1; j <= centre + reach; ++j)
        {
            const double pos = std::abs (t - (double) j) * tableScale;
            const auto idx = (size_t) pos;
            if (idx + 1 >= table.size())
                continue;
            const double frac = pos - (double) idx;
            const double w = table[idx] + frac * (table[idx + 1] - table[idx]);
            wsum += w;
            if (j >= 0 && j < numIn)
                acc += w * in[(size_t) j];
        }

        out[n] = wsum > 1e-9 ? (float) (acc / wsum) : 0.0f;
    }

    return out;
}

} // namespace so::analysis
