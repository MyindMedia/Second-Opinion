#include "Ranker.h"

#include "Harmony.h"
#include "../theory/ChordNamer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace so::reharm
{

double HeuristicRanker::score (const PlanFeatures& f) const
{
    return 3.0 * f.melodyFit + 3.0 * f.tensionMatch + 1.0 * f.voiceLeading + 1.5 * f.novelty + 2.0 * f.tensionPush;
}

namespace
{
    using Segs = std::vector<analysis::ChordSegment>;

    bool sameSpans (const Candidate& a, const Candidate& b)
    {
        if (a.spans.size() != b.spans.size())
            return false;
        for (size_t i = 0; i < a.spans.size(); ++i)
            if (! a.spans[i].chord.sameHarmony (b.spans[i].chord) || std::abs (a.spans[i].beats - b.spans[i].beats) > 1.0e-9)
                return false;
        return true;
    }

    bool changesStart (const Candidate& c) { return ! c.insertion; }

    /** Adds picks greedily, skipping overlaps and picks whose resolution target gets replaced. */
    std::vector<const Candidate*> consistent (std::vector<const Candidate*> wanted)
    {
        std::map<int, const Candidate*> bySeg;
        for (const auto* c : wanted)
        {
            if (bySeg.count (c->segmentIndex) > 0)
                continue;
            bool ok = true;
            if (c->targetSegment >= 0)
            {
                auto it = bySeg.find (c->targetSegment);
                if (it != bySeg.end() && changesStart (*it->second) && it->second->groupKey() != c->groupKey())
                    ok = false;
            }
            for (const auto& [seg, other] : bySeg)
                if (other->targetSegment == c->segmentIndex && changesStart (*c) && other->groupKey() != c->groupKey())
                    ok = false;
            if (ok)
                bySeg[c->segmentIndex] = c;
        }
        std::vector<const Candidate*> out;
        for (const auto& [seg, c] : bySeg)
            out.push_back (c);
        return out;
    }

    std::string planKey (const std::vector<const Candidate*>& picks)
    {
        std::string k;
        for (const auto* c : picks)
        {
            k += std::to_string (c->segmentIndex) + "=";
            for (const auto& sp : c->spans)
                k += theory::ChordNamer::name (sp.chord, { theory::NamingOptions::Style::Pop, true,
                                                           theory::NamingOptions::Spelling::Auto,
                                                           theory::AccidentalStyle::Ascii }) + ",";
            k += ";";
        }
        return k;
    }

    /** A change that lands on the same chord as its neighbour only smears the harmony. */
    bool repeatsNeighbour (const Segs& segs, const std::map<int, const Candidate*>& picks)
    {
        std::vector<std::pair<Chord, bool>> seq;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            auto it = picks.find ((int) i);
            if (it == picks.end())
                seq.push_back ({ segs[i].chord, false });
            else
                for (const auto& sp : it->second->spans)
                    seq.push_back ({ sp.chord, ! sp.chord.sameHarmony (segs[i].chord) });
        }
        for (size_t i = 1; i < seq.size(); ++i)
            if ((seq[i].second || seq[i - 1].second) && seq[i].first.sameHarmony (seq[i - 1].first))
                return true;
        return false;
    }

    double vlOf (const Segs& segs, const std::map<int, const Candidate*>& picks)
    {
        std::vector<Chord> seq;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            auto it = picks.find ((int) i);
            if (it == picks.end())
                seq.push_back (segs[i].chord);
            else
                for (const auto& sp : it->second->spans)
                    seq.push_back (sp.chord);
        }
        if (seq.size() < 2)
            return 0.0;
        double total = 0.0;
        for (size_t i = 1; i < seq.size(); ++i)
            total += pcDistance (seq[i - 1], seq[i]);
        return total / (double) (seq.size() - 1);
    }
}

std::vector<Plan> Ranker::rank (const std::vector<Candidate>& candidates, const Segs& segs,
                                const theory::Key& key, const Settings& settingsIn, int count, const IRanker& scorer)
{
    const auto s = settingsIn.sanitized();
    const double target = targetChordTension (s.tension);

    // Best candidate per (group, segment).
    std::map<std::string, std::map<int, const Candidate*>> groups;
    for (const auto& c : candidates)
    {
        auto& slot = groups[c.groupKey()][c.segmentIndex];
        if (slot == nullptr || c.score > slot->score)
            slot = &c;
    }

    auto nextOf = [&] (int i) { return segs.size() < 2 ? -1 : (int) (((size_t) i + 1) % segs.size()); };

    // Same idea wherever the same chord move recurs (every time the loop comes round).
    auto expand = [&] (const std::map<int, const Candidate*>& g, const Candidate* c)
    {
        std::vector<const Candidate*> out;
        const auto& orig = segs[(size_t) c->segmentIndex].chord;
        const int n0 = nextOf (c->segmentIndex);
        for (const auto& [seg, o] : g)
        {
            if (! segs[(size_t) seg].chord.sameHarmony (orig) || ! sameSpans (*o, *c))
                continue;
            const int n1 = nextOf (seg);
            if (c->targetSegment >= 0 && (n0 < 0 || n1 < 0 || ! segs[(size_t) n0].chord.sameHarmony (segs[(size_t) n1].chord)))
                continue;
            out.push_back (o);
        }
        return out;
    };

    std::vector<std::pair<std::vector<const Candidate*>, std::vector<std::string>>> raw;
    std::vector<std::pair<double, std::string>> groupStrength;

    for (const auto& [gk, g] : groups)
    {
        std::vector<const Candidate*> list;
        for (const auto& [seg, c] : g)
            list.push_back (c);
        std::stable_sort (list.begin(), list.end(), [] (const Candidate* a, const Candidate* b) { return a->score > b->score; });

        const auto* best = list.front();
        groupStrength.push_back ({ best->score, gk });

        auto one = expand (g, best);
        raw.push_back ({ one, { gk } });

        for (const auto* c : list)
        {
            if (segs[(size_t) c->segmentIndex].chord.sameHarmony (segs[(size_t) best->segmentIndex].chord))
                continue;
            auto two = one;
            for (const auto* o : expand (g, c))
                two.push_back (o);
            raw.push_back ({ two, { gk } });
            break;
        }

        // "Everywhere it fits", capped at a third of the segments so the idea stays a colour.
        std::vector<const Candidate*> all;
        const size_t cap = std::max<size_t> (2, (segs.size() + 2) / 3);
        for (const auto* c : list)
            if (c->score >= 0.8 * best->score && all.size() < cap)
                all.push_back (c);
        raw.push_back ({ all, { gk } });
    }

    // Pairs of strong ideas from different techniques.
    std::stable_sort (groupStrength.begin(), groupStrength.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    const size_t topGroups = std::min<size_t> (groupStrength.size(), 10);
    for (size_t a = 0; a < topGroups; ++a)
        for (size_t b = a + 1; b < topGroups; ++b)
        {
            const auto& ga = groups[groupStrength[a].second];
            const auto& gb = groups[groupStrength[b].second];
            if (ga.begin()->second->technique == gb.begin()->second->technique)
                continue;
            auto best = [] (const std::map<int, const Candidate*>& g)
            {
                const Candidate* bc = nullptr;
                for (const auto& [seg, c] : g)
                    if (bc == nullptr || c->score > bc->score)
                        bc = c;
                return bc;
            };
            auto pa = expand (ga, best (ga));
            for (const auto* c : expand (gb, best (gb)))
                pa.push_back (c);
            raw.push_back ({ pa, { groupStrength[a].second, groupStrength[b].second } });
        }

    // Score unique, consistent plans.
    double totalBeats = 0.0;
    for (const auto& sg : segs)
        totalBeats += sg.beats;
    const double origVl = vlOf (segs, {});
    double origTensionSum = 0.0;
    std::vector<double> segTension;
    for (const auto& sg : segs)
    {
        segTension.push_back (chordTension (sg.chord, key) * sg.beats);
        origTensionSum += segTension.back();
    }
    const double origMean = totalBeats > 0.0 ? origTensionSum / totalBeats : 0.0;
    const double knob = (s.tension - 50) / 50.0;

    std::vector<Plan> plans;
    std::set<std::string> seenKeys;
    for (auto& [wanted, gks] : raw)
    {
        std::stable_sort (wanted.begin(), wanted.end(), [] (const Candidate* x, const Candidate* y) { return x->score > y->score; });
        auto picks = consistent (wanted);
        if (picks.empty())
            continue;

        Plan p;
        p.picks = picks;
        p.key = planKey (picks);
        if (! seenKeys.insert (p.key).second)
            continue;

        std::set<std::string> present;
        for (const auto* c : picks)
            present.insert (c->groupKey());
        for (const auto& g : gks)
            if (present.count (g) > 0)
                p.groups.push_back (g);
        if (p.groups.empty())
            continue;

        double fit = 0.0, ten = 0.0, changed = 0.0, overall = origTensionSum;
        std::map<int, const Candidate*> bySeg;
        for (const auto* c : picks)
        {
            fit += c->melodyFit * c->changedBeats;
            ten += c->tension * c->changedBeats;
            changed += c->changedBeats;
            bySeg[c->segmentIndex] = c;
            overall -= segTension[(size_t) c->segmentIndex];
            for (const auto& sp : c->spans)
                overall += chordTension (sp.chord, key) * sp.beats;
        }
        if (repeatsNeighbour (segs, bySeg))
            continue;
        p.features.melodyFit = fit / changed;
        p.changedTension = ten / changed;
        p.features.tensionMatch = 1.0 - std::min (1.0, std::abs (p.changedTension - target) / 80.0);
        p.features.voiceLeading = std::clamp (1.0 - (vlOf (segs, bySeg) - origVl) / 4.0, 0.0, 1.0);
        p.overallTension = totalBeats > 0.0 ? overall / totalBeats : 0.0;
        p.features.tensionPush = std::clamp (knob * (p.overallTension - origMean) / 10.0, -1.0, 1.0);
        const double n = totalBeats > 0.0 ? changed / totalBeats : 0.0;
        p.features.novelty = std::clamp (std::min (1.0, n / 0.2) - std::max (0.0, (n - 0.6) * 2.5), 0.0, 1.0);

        p.score = scorer.score (p.features) + 0.8 * unitNoise (s.seed, "plan:" + p.key);
        plans.push_back (std::move (p));
    }

    // MMR selection for a diverse set.
    auto changeSet = [] (const Plan& p)
    {
        std::set<std::string> cs;
        for (const auto* c : p.picks)
            cs.insert (std::to_string (c->segmentIndex) + ":" + c->groupKey());
        return cs;
    };
    auto similarity = [&] (const Plan& a, const Plan& b)
    {
        const auto ca = changeSet (a), cb = changeSet (b);
        size_t inter = 0;
        for (const auto& x : ca)
            inter += cb.count (x);
        const double jac = (double) inter / (double) (ca.size() + cb.size() - inter);
        double tech = 0.0;
        for (const auto& g : a.groups)
            if (std::find (b.groups.begin(), b.groups.end(), g) != b.groups.end())
                tech = 1.0;
        if (tech == 0.0)
            for (const auto* x : a.picks)
                for (const auto* y : b.picks)
                    if (x->technique == y->technique)
                        tech = 0.5;
        return 0.6 * jac + 0.4 * tech;
    };

    std::vector<Plan> chosen;
    std::vector<bool> taken (plans.size(), false);
    const double lambda = 2.5;
    while ((int) chosen.size() < count)
    {
        int arg = -1;
        double best = -1.0e300;
        for (size_t i = 0; i < plans.size(); ++i)
        {
            if (taken[i])
                continue;
            double maxSim = 0.0;
            for (const auto& c : chosen)
                maxSim = std::max (maxSim, similarity (plans[i], c));
            const double v = plans[i].score - lambda * maxSim;
            if (v > best)
            {
                best = v;
                arg = (int) i;
            }
        }
        if (arg < 0)
            break;
        taken[(size_t) arg] = true;
        chosen.push_back (plans[(size_t) arg]);
    }
    return chosen;
}

} // namespace so::reharm
