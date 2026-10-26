#include "BeatAggregator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace so::analysis
{

std::vector<BeatFeatures> aggregateBeats (const ChromaFrames& frames, const BeatGrid& grid, int numBeats)
{
    std::vector<BeatFeatures> beats ((size_t) std::max (0, numBeats));
    if (frames.size() == 0 || beats.empty())
        return beats;

    size_t f = 0;
    float maxEnergy = 0.0f;

    for (int b = 0; b < numBeats; ++b)
    {
        const double s0 = grid.beatToSec (b);
        const double s1 = grid.beatToSec (b + 1);
        auto& out = beats[(size_t) b];

        while (f < frames.size() && frames.times[f] < s0)
            ++f;

        double wsum = 0.0, esum = 0.0;
        for (size_t i = f; i < frames.size() && frames.times[i] < s1; ++i)
        {
            const double x = (frames.times[i] - s0) / (s1 - s0);
            const double w = 0.25 + 0.75 * std::sin (std::numbers::pi * x);
            for (size_t k = 0; k < 12; ++k)
            {
                out.chroma[k] += (float) w * frames.chroma[i][k];
                out.bass[k] += (float) w * frames.bass[i][k];
            }
            esum += w * frames.energy[i];
            wsum += w;
        }

        if (wsum <= 0.0)
        {
            // No frame centre inside a very short beat: take the nearest frame.
            const double mid = 0.5 * (s0 + s1);
            const auto it = std::lower_bound (frames.times.begin(), frames.times.end(), mid);
            size_t i = (size_t) std::distance (frames.times.begin(), it);
            if (i >= frames.size())
                i = frames.size() - 1;
            if (i > 0 && std::abs (frames.times[i - 1] - mid) < std::abs (frames.times[i] - mid))
                --i;
            if (frames.times[i] >= s0 - (s1 - s0) && frames.times[i] < s1 + (s1 - s0))
            {
                out.chroma = frames.chroma[i];
                out.bass = frames.bass[i];
                out.energy = frames.energy[i];
            }
        }
        else
        {
            for (size_t k = 0; k < 12; ++k)
            {
                out.chroma[k] = (float) (out.chroma[k] / wsum);
                out.bass[k] = (float) (out.bass[k] / wsum);
            }
            out.energy = (float) (esum / wsum);
        }

        maxEnergy = std::max (maxEnergy, out.energy);
    }

    const float gate = std::max (1.0e-4f, maxEnergy * 0.0316f);
    for (auto& b : beats)
        b.silent = b.energy < gate;

    return beats;
}

Chroma normalizeChroma (const Chroma& c, float floor) noexcept
{
    Chroma out {};
    float mx = 0.0f;
    for (float v : c)
        mx = std::max (mx, v);
    if (mx <= 0.0f)
        return out;

    const float scale = 1.0f / (1.0f - floor);
    for (size_t i = 0; i < 12; ++i)
        out[i] = std::max (0.0f, c[i] / mx - floor) * scale;
    return out;
}

} // namespace so::analysis
