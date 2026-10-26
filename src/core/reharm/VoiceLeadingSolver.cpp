#include "VoiceLeadingSolver.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <utility>

namespace so::reharm
{

using theory::wrapPc;

SolverWeights SolverWeights::fromSmoothness (int smoothness) noexcept
{
    SolverWeights w;
    w.alpha = 0.15 + 2.85 * std::clamp (smoothness, 0, 100) / 100.0;
    return w;
}

VoiceLeadingSolver::VoiceLeadingSolver (SolverOptions options)
    : opt (options), w (SolverWeights::fromSmoothness (options.smoothness))
{
    opt.density = std::clamp (opt.density, 3, 6);
    opt.rangeHigh = std::max (opt.rangeHigh, opt.rangeLow + 12);
}

int VoiceLeadingSolver::movement (const std::vector<int>& a, const std::vector<int>& b) noexcept
{
    if (a.empty() || b.empty())
        return 0;
    int total = 0;
    const size_t n = std::max (a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        total += std::abs (b[std::min (i, b.size() - 1)] - a[std::min (i, a.size() - 1)]);
    return total;
}

namespace
{
    struct Tone
    {
        int pc;
        double priority;      // higher = more essential
        double doubleCost;    // cost of a second copy
    };

    std::vector<Tone> tonesOf (const theory::Chord& c, int bassPc)
    {
        std::vector<Tone> tones;
        const bool sus = theory::info (c.quality).family == theory::QualityFamily::Suspended;
        for (const auto& iv : theory::info (c.quality).intervals)
        {
            const int pc = wrapPc (c.root.pc() + iv.semitones);
            Tone t { pc, 1.0, 0.5 };
            switch (iv.degree)
            {
                case 1:  t = { pc, bassPc == pc ? 0.5 : 3.5, 0.3 }; break;
                case 3:  t = { pc, bassPc == pc ? 2.0 : 6.0, 3.0 }; break;
                case 2:
                case 4:  t = { pc, sus ? 6.0 : 3.0, 2.0 }; break;
                case 5:  t = { pc, iv.semitones == 7 ? 1.0 : 4.0, iv.semitones == 7 ? 0.5 : 2.0 }; break;
                case 6:
                case 7:  t = { pc, 5.0, 4.0 }; break;
                case 9:  t = { pc, 3.0, 1.5 }; break;
                default: break;
            }
            tones.push_back (t);
        }
        return tones;
    }

    double centreOf (const SolverOptions& o)
    {
        const double mid = 0.5 * (o.rangeLow + o.rangeHigh);
        return std::clamp (0.5 * mid + 0.5 * 62.0, o.rangeLow + 6.0, o.rangeHigh - 6.0);
    }

    double spacingCost (const std::vector<int>& n)
    {
        double c = 0.0;
        int below = 0;
        for (size_t i = 0; i < n.size(); ++i)
        {
            if (n[i] < 48)
                ++below;
            if (i + 1 == n.size())
                break;
            const int a = n[i];
            const int g = n[i + 1] - a;
            if (a < 48 && g <= 2)
                c += 8.0;                                  // no seconds below C3
            const int lim = a < 40 ? 7 : a < 45 ? 5 : a < 48 ? 4 : a < 52 ? 3 : 1;
            if (g < lim)
                c += 4.0;                                  // low-interval limits
            if (g == 1)
                c += (i + 2 == n.size()) ? 3.0 : 1.5;      // minor 2nds, harsher on top
            if (i > 0 && g > 9)
                c += 0.3 * (g - 9);                        // holes in the upper voices
        }
        if (below > 1)
            c += 2.0 * (below - 1);                        // mud
        return c;
    }

    std::uint64_t perfectMask (const std::vector<int>& n)
    {
        std::uint64_t m = 0;
        for (size_t i = 0; i < n.size(); ++i)
            for (size_t j = i + 1; j < n.size(); ++j)
            {
                const int ic = (n[j] - n[i]) % 12;
                if (ic == 0 || ic == 7)
                    m |= std::uint64_t { 1 } << (i * 6 + j);
            }
        return m;
    }
}

std::vector<Voicing> VoiceLeadingSolver::enumerate (const VoicingRequest& r, bool fixedTop) const
{
    const int bassPc = r.bassNote >= 0 ? wrapPc (r.bassNote) : r.chord.bassPc();
    auto tones = tonesOf (r.chord, bassPc);
    std::stable_sort (tones.begin(), tones.end(), [] (const Tone& a, const Tone& b) { return a.priority > b.priority; });

    const int top = r.topTarget.value_or (-1);
    const int slots = fixedTop ? opt.density - 1 : opt.density;
    const int topPc = fixedTop ? wrapPc (top) : -1;

    // Required core: essential tones (3rd/sus, 7th/6th, 9th, altered 5th, uncovered root), capped by density.
    std::array<int, 12> base {};
    double baseCost = 0.0;
    int used = 0;
    bool topCovered = false;
    std::vector<Tone> pool;
    for (const auto& t : tones)
    {
        if (fixedTop && t.pc == topPc && ! topCovered)
        {
            topCovered = true;     // the melody note on top already sounds this tone
            pool.push_back (t);
            continue;
        }
        if (t.priority >= 2.0 && used < slots)
        {
            ++base[(size_t) t.pc];
            ++used;
            pool.push_back (t);
        }
        else
        {
            pool.push_back ({ t.pc, t.priority, 0.0 });     // first copy of an optional tone is free
            if (t.priority >= 2.0)
                baseCost += 2.0;                            // an essential tone had to be dropped
        }
    }

    const int lowFloor = std::max (opt.rangeLow, r.bassNote + 1);
    const int high = fixedTop ? top - 1 : opt.rangeHigh;
    const int maxGap = 12;
    const int maxSpan = 12 + 4 * (opt.density - 2);
    const double centre = centreOf (opt);

    std::vector<Voicing> out;
    std::set<std::vector<int>> seen;

    // Fill the remaining slots from the pool (combinations with repetition).
    const int k = slots - used;
    std::vector<size_t> pick ((size_t) std::max (0, k), 0);

    auto runFill = [&] (const std::array<int, 12>& counts, double fillCost)
    {
        std::vector<int> notes;
        notes.reserve ((size_t) opt.density);
        std::array<int, 12> left = counts;

        auto finish = [&]
        {
            std::vector<int> v = notes;
            if (fixedTop)
            {
                if (v.empty() || top - v.back() > maxGap || top - v.front() > maxSpan || top > opt.rangeHigh)
                    return;
                v.push_back (top);
            }
            if (! seen.insert (v).second)
                return;

            double mean = 0.0;
            for (int x : v) mean += x;
            mean /= (double) v.size();

            Voicing vc;
            vc.staticCost = baseCost + fillCost + w.gamma * std::abs (mean - centre) + w.delta * spacingCost (v);
            if (r.bassNote >= 0)
            {
                const int gapToBass = v.front() - r.bassNote;
                if (gapToBass < 3) vc.staticCost += 2.0 * w.delta;
                const int ic = wrapPc (gapToBass);
                if (ic == 1 || ic == 11) vc.staticCost += 2.0 * w.delta;
            }
            if (! fixedTop && r.topTarget)
                vc.staticCost += w.epsilon * (std::abs (v.back() - *r.topTarget) + 2.5);
            vc.perfectPairs = perfectMask (v);
            vc.notes = std::move (v);
            out.push_back (std::move (vc));
        };

        auto rec = [&] (auto& self, int pos) -> void
        {
            if (pos == slots)
            {
                finish();
                return;
            }
            const int from = pos == 0 ? lowFloor : notes.back() + 1;
            const int to = pos == 0 ? high : std::min (high, notes.back() + maxGap);
            for (int n = from; n <= to; ++n)
            {
                const int pc = wrapPc (n);
                if (left[(size_t) pc] == 0)
                    continue;
                if (pos > 0 && n - notes.front() > maxSpan)
                    break;
                --left[(size_t) pc];
                notes.push_back (n);
                self (self, pos + 1);
                notes.pop_back();
                ++left[(size_t) pc];
            }
        };
        rec (rec, 0);
    };

    if (k <= 0)
        runFill (base, 0.0);
    else
    {
        // Enumerate non-decreasing index tuples into the pool.
        auto next = [&]() -> bool
        {
            int i = k - 1;
            while (i >= 0 && pick[(size_t) i] == pool.size() - 1)
                --i;
            if (i < 0)
                return false;
            ++pick[(size_t) i];
            for (int j = i + 1; j < k; ++j)
                pick[(size_t) j] = pick[(size_t) i];
            return true;
        };
        do
        {
            auto counts = base;
            double cost = 0.0;
            for (size_t idx : pick)
            {
                const auto& t = pool[idx];
                const bool already = counts[(size_t) t.pc] > 0 || (fixedTop && t.pc == topPc);
                cost += already ? std::max (t.doubleCost, 0.3) : t.doubleCost;
                ++counts[(size_t) t.pc];
            }
            runFill (counts, cost);
        } while (next());
    }

    std::sort (out.begin(), out.end(), [] (const Voicing& a, const Voicing& b)
    {
        if (a.staticCost != b.staticCost) return a.staticCost < b.staticCost;
        return a.notes < b.notes;
    });

    // Prune: at most two per (bottom, second-from-top) bucket, then the best 64 overall.
    std::map<std::pair<int, int>, int> buckets;
    std::vector<Voicing> kept;
    for (auto& v : out)
    {
        const auto key = std::make_pair (v.notes.front(), v.notes[v.notes.size() - 2]);
        if (++buckets[key] > 2)
            continue;
        kept.push_back (std::move (v));
        if (kept.size() >= 64)
            break;
    }
    return kept;
}

const std::vector<Voicing>& VoiceLeadingSolver::candidates (const VoicingRequest& r)
{
    const std::string key = std::to_string (r.chord.root.pc()) + "," + std::to_string ((int) r.chord.quality) + ","
                          + std::to_string (r.chord.bassPc()) + "," + std::to_string (r.bassNote) + ","
                          + std::to_string (r.topTarget.value_or (-1));
    auto it = cache.find (key);
    if (it != cache.end())
        return it->second;

    std::vector<Voicing> v;
    if (r.topTarget && *r.topTarget > opt.rangeLow + 6 && *r.topTarget <= opt.rangeHigh)
        v = enumerate (r, true);
    if (v.empty())
        v = enumerate (r, false);
    if (v.empty())
    {
        // Degenerate range: stack chord tones upward from the floor so there is always an answer.
        Voicing f;
        const auto pcs = r.chord.chordTonePcs();
        int n = std::max (opt.rangeLow, r.bassNote + 1);
        for (int i = 0; i < opt.density; ++i)
        {
            const int pc = pcs[(size_t) i % pcs.size()];
            while (wrapPc (n) != pc) ++n;
            f.notes.push_back (n);
            ++n;
        }
        f.perfectPairs = perfectMask (f.notes);
        f.staticCost = 1000.0;
        v.push_back (std::move (f));
    }
    return cache.emplace (key, std::move (v)).first->second;
}

double VoiceLeadingSolver::transition (const Voicing& a, const Voicing& b, int bassA, int bassB) const
{
    double cost = w.alpha * movement (a.notes, b.notes);

    int parallels = 0;
    std::uint64_t m = a.perfectPairs & b.perfectPairs;
    while (m != 0)
    {
        const int bit = std::countr_zero (m);
        m &= m - 1;
        const size_t i = (size_t) (bit / 6), j = (size_t) (bit % 6);
        if ((a.notes[j] - a.notes[i]) % 12 != (b.notes[j] - b.notes[i]) % 12)
            continue;
        const int di = b.notes[i] - a.notes[i], dj = b.notes[j] - a.notes[j];
        if (di != 0 && dj != 0 && (di > 0) == (dj > 0))
            ++parallels;
    }
    if (bassA >= 0 && bassB >= 0)
    {
        const int ia = wrapPc (a.notes.back() - bassA), ib = wrapPc (b.notes.back() - bassB);
        const int dt = b.notes.back() - a.notes.back(), db = bassB - bassA;
        if (ia == ib && (ia == 0 || ia == 7) && dt != 0 && db != 0 && (dt > 0) == (db > 0))
            ++parallels;
    }
    return cost + w.beta * parallels;
}

std::vector<std::vector<int>> VoiceLeadingSolver::solve (const std::vector<VoicingRequest>& requests)
{
    const size_t n = requests.size();
    std::vector<std::vector<int>> result (n);
    if (n == 0)
        return result;

    std::vector<const std::vector<Voicing>*> cands (n);
    std::vector<size_t> sizes (n);
    for (size_t i = 0; i < n; ++i)
    {
        cands[i] = &candidates (requests[i]);
        sizes[i] = requests[i].locked ? 1 : cands[i]->size();
    }

    std::vector<std::vector<double>> cost (n);
    std::vector<std::vector<int>> back (n);
    cost[0].resize (sizes[0]);
    back[0].assign (sizes[0], -1);
    for (size_t k = 0; k < sizes[0]; ++k)
        cost[0][k] = (*cands[0])[k].staticCost;

    for (size_t i = 1; i < n; ++i)
    {
        cost[i].assign (sizes[i], std::numeric_limits<double>::infinity());
        back[i].assign (sizes[i], 0);
        for (size_t k = 0; k < sizes[i]; ++k)
        {
            const auto& vb = (*cands[i])[k];
            double best = std::numeric_limits<double>::infinity();
            int arg = 0;
            for (size_t p = 0; p < sizes[i - 1]; ++p)
            {
                const double c = cost[i - 1][p]
                               + transition ((*cands[i - 1])[p], vb, requests[i - 1].bassNote, requests[i].bassNote);
                if (c < best)
                {
                    best = c;
                    arg = (int) p;
                }
            }
            cost[i][k] = best + vb.staticCost;
            back[i][k] = arg;
        }
    }

    size_t k = 0;
    for (size_t q = 1; q < sizes[n - 1]; ++q)
        if (cost[n - 1][q] < cost[n - 1][k])
            k = q;
    for (size_t i = n; i-- > 0;)
    {
        result[i] = (*cands[i])[k].notes;
        if (i > 0)
            k = (size_t) back[i][k];
    }
    return result;
}

} // namespace so::reharm
