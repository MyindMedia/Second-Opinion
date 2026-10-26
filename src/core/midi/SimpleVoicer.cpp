#include "SimpleVoicer.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace so::midi
{

namespace
{
    constexpr int upperLow = 48;   // C3
    constexpr int upperHigh = 84;  // C6
    constexpr int bassLow = 28;    // E1
    constexpr int bassHigh = 48;   // C3

    std::vector<int> closeVoicing (const std::vector<int>& pcs, int rotation, int startOctaveC)
    {
        std::vector<int> v;
        int prev = -1;
        for (size_t i = 0; i < pcs.size(); ++i)
        {
            const int pc = pcs[(i + (size_t) rotation) % pcs.size()];
            int n = startOctaveC + pc;
            while (n <= prev) n += 12;
            v.push_back (n);
            prev = n;
        }
        return v;
    }

    int movement (const std::vector<int>& a, const std::vector<int>& b)
    {
        if (a.empty()) return 0;
        // Compare sorted voices pairwise, padding with the nearest note for size differences.
        int total = 0;
        for (size_t i = 0; i < b.size(); ++i)
            total += std::abs (b[i] - a[std::min (i, a.size() - 1)]);
        return total;
    }
}

VoicedLanes voiceProgression (const std::vector<TimedChord>& progression)
{
    VoicedLanes lanes;
    std::vector<int> prevUpper;
    int prevBass = 38; // D2, middle of the bass range

    for (const auto& tc : progression)
    {
        auto pcs = tc.chord.chordTonePcs();

        // Keep at most four upper voices: drop the 5th first on 5-note chords (it adds the least color).
        if (pcs.size() > 4)
            pcs.erase (pcs.begin() + 2);

        std::vector<int> best;
        int bestCost = INT_MAX;

        for (int rot = 0; rot < (int) pcs.size(); ++rot)
        {
            for (int base = upperLow; base <= upperHigh - 12; base += 12)
            {
                auto v = closeVoicing (pcs, rot, base);
                if (v.front() < 55 || v.back() > upperHigh) // keep the bottom of the stack above G3, avoids mud
                    continue;

                const int centre = (v.front() + v.back()) / 2;
                const int cost = prevUpper.empty() ? std::abs (centre - 66) : movement (prevUpper, v);
                if (cost < bestCost) { bestCost = cost; best = v; }
            }
        }

        for (int n : best)
            lanes.chords.notes.push_back ({ n, tc.startBeat, tc.lengthBeats, 88, 0 });

        if (! best.empty())
            lanes.top.notes.push_back ({ best.back(), tc.startBeat, tc.lengthBeats, 96, 0 });

        const int bpc = tc.chord.bassPc();
        int bassNote = bassLow + ((bpc - bassLow) % 12 + 12) % 12;
        int bestBass = bassNote, bestDist = INT_MAX;
        for (int n = bassNote; n <= bassHigh; n += 12)
            if (std::abs (n - prevBass) < bestDist) { bestDist = std::abs (n - prevBass); bestBass = n; }

        lanes.bass.notes.push_back ({ bestBass, tc.startBeat, tc.lengthBeats, 100, 0 });

        prevUpper = best;
        prevBass = bestBass;
    }

    return lanes;
}

} // namespace so::midi
