#include "Reharmonizer.h"

#include "BassGenerator.h"
#include "CandidateGenerator.h"
#include "Explainer.h"
#include "Harmony.h"
#include "Ranker.h"
#include "VoiceLeadingSolver.h"
#include "../theory/ChordNamer.h"
#include "../theory/RomanNumeral.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace so::reharm
{

namespace
{
    struct Entry
    {
        Span span;
        int segmentIndex { 0 };
        bool changed { false };
        bool locked { false };
        bool inserted { false };
        Technique technique { Technique::Original };
        std::string variant;
        theory::Chord original;
    };

    std::string titleFor (Technique t, const std::string& variant)
    {
        switch (t)
        {
            case Technique::Original:          return "Original";
            case Technique::TritoneSub:        return "Tritone slide";
            case Technique::ChromaticApproach: return "Chromatic approach";
            case Technique::SecondaryDominant: return "Secondary " + variant + " push";
            case Technique::IiVInsertion:      return "ii-V setup";
            case Technique::ModalInterchange:
            {
                const bool major = ! variant.empty() && variant.find_first_of ("IV") != std::string::npos;
                return "Borrowed " + variant + (major ? " lift" : " shade");
            }
            case Technique::Backdoor:          return "Back-door resolution";
            case Technique::Extension:         return variant == "simpler" ? "Stripped-back triads" : "Lush extensions";
            case Technique::DiatonicSub:       return variant == "relative" ? "Relative swap"
                                                    : variant == "mediant" ? "Mediant swap" : "Diatonic swap";
            case Technique::ChromaticMediant:  return "Chromatic mediant shift";
            case Technique::NeoRiemannianP:    return "Neo-Riemannian P flip";
            case Technique::NeoRiemannianL:    return "Neo-Riemannian L flip";
            case Technique::NeoRiemannianR:    return "Neo-Riemannian R flip";
        }
        return "Suggestion";
    }

    std::string lowerFirst (std::string s)
    {
        if (! s.empty() && s[0] >= 'A' && s[0] <= 'Z' && ! (s.size() > 1 && s[1] == '-'))
            s[0] = (char) (s[0] - 'A' + 'a');
        return s;
    }

    std::optional<int> topTargetFor (const analysis::AnalysisResult& a, const Settings& s, const Span& sp)
    {
        if (s.melodyOnTop == MelodyOnTop::Off)
            return std::nullopt;
        const auto m = melodyAt (a.melody, sp.startBeat, std::min (sp.beats, 1.0));
        if (! m)
            return std::nullopt;
        if (s.melodyOnTop == MelodyOnTop::Exact)
            return m;

        const int a1 = s.melodyOnTop == MelodyOnTop::ThirdBelow ? 3 : 8;
        // Prefer a chord tone a 3rd (or 6th) below, then an allowed tension.
        for (int pass = 0; pass < 2; ++pass)
            for (int d : { a1, a1 + 1 })
            {
                const auto role = classifyTone (sp.chord, *m - d, s.tension);
                if ((pass == 0 && role == ToneRole::ChordTone) || (pass == 1 && role == ToneRole::Tension))
                    return *m - d;
            }
        return *m - a1;
    }

    Suggestion build (const analysis::AnalysisResult& a, const Settings& s,
                      const std::vector<analysis::ChordSegment>& segs, const std::vector<bool>& lockedSeg,
                      const std::map<int, const Candidate*>& picks, const std::vector<std::string>& groups,
                      VoiceLeadingSolver& solver)
    {
        const auto& key = a.key;
        const int bpb = std::max (1, a.grid.beatsPerBar);

        std::vector<Entry> es;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const auto& orig = segs[i].chord;
            auto it = picks.find ((int) i);
            if (it == picks.end())
            {
                es.push_back ({ { segs[i].startBeat, segs[i].beats, orig }, (int) i, false, lockedSeg[i], false,
                                Technique::Original, {}, orig });
                continue;
            }
            const auto* c = it->second;
            for (size_t k = 0; k < c->spans.size(); ++k)
            {
                const auto& sp = c->spans[k];
                const bool changed = ! (sp.chord.sameHarmony (orig) && sp.chord.bassPc() == orig.bassPc());
                es.push_back ({ sp, (int) i, changed, false, changed && c->insertion,
                                changed ? c->technique : Technique::Original, c->variant, orig });
            }
        }

        // Bass first: its notes are hard floors for the upper voicing.
        std::vector<BassSpan> bs;
        for (const auto& e : es)
            bs.push_back ({ e.span.chord, e.span.startBeat, e.span.beats, e.locked,
                            segs[(size_t) e.segmentIndex].chord.isSlash() });
        const auto bass = BassGenerator::generate (bs, s, key, bpb);

        std::vector<VoicingRequest> reqs;
        for (size_t i = 0; i < es.size(); ++i)
            reqs.push_back ({ es[i].span.chord, bass.spanBass[i], topTargetFor (a, s, es[i].span), es[i].locked });
        const auto voicings = solver.solve (reqs);

        Suggestion out;
        out.lanes.bass.notes = bass.notes;
        double moveTotal = 0.0, tenSum = 0.0, beatSum = 0.0;
        int changedCount = 0;
        for (size_t i = 0; i < es.size(); ++i)
        {
            const auto& e = es[i];
            for (int n : voicings[i])
                out.lanes.chords.notes.push_back ({ n, e.span.startBeat, e.span.beats, 80, 0 });
            out.lanes.top.notes.push_back ({ voicings[i].back(), e.span.startBeat, e.span.beats, 92, 0 });
            if (i > 0)
                moveTotal += VoiceLeadingSolver::movement (voicings[i - 1], voicings[i]);

            SuggestionChord sc;
            sc.segmentId = segs[(size_t) e.segmentIndex].id;
            sc.startBeat = e.span.startBeat;
            sc.beats = e.span.beats;
            sc.chord = e.span.chord;
            sc.symbol = theory::ChordNamer::name (e.span.chord);
            sc.changed = e.changed;
            sc.technique = e.technique;
            const auto rn = theory::RomanNumeral::analyze (e.span.chord, key);
            sc.roman = rn.numeral;
            sc.function = rn.function;
            sc.tension = chordTension (e.span.chord, key);
            tenSum += sc.tension * e.span.beats;
            beatSum += e.span.beats;
            if (e.changed)
                ++changedCount;

            ExplainContext ctx;
            ctx.chord = e.span.chord;
            ctx.original = e.original;
            if (es.size() > 1)
                ctx.next = es[(i + 1) % es.size()].span.chord;
            ctx.key = key;
            ctx.technique = e.technique;
            ctx.variant = e.variant;
            ctx.inserted = e.inserted;
            const auto hits = melodyHits (a.melody, e.span.startBeat, e.span.beats, bpb);
            for (const auto& h : hits)
                if (h.strong) { ctx.melodyPitch = h.pitch; break; }
            if (! ctx.melodyPitch && ! hits.empty())
                ctx.melodyPitch = hits.front().pitch;
            sc.explanation = Explainer::explain (ctx);
            sc.roman = sc.explanation.roman;
            sc.function = sc.explanation.function;
            out.chords.push_back (std::move (sc));
        }

        out.movement = es.size() > 1 ? moveTotal / (double) (es.size() - 1) : 0.0;
        out.tension = beatSum > 0.0 ? (int) std::lround (tenSum / beatSum) : 0;

        // Techniques and families in plan order (primary idea first).
        for (const auto& [seg, c] : picks)
            if (std::find (out.techniques.begin(), out.techniques.end(), c->technique) == out.techniques.end())
                out.techniques.push_back (c->technique);
        std::stable_sort (out.techniques.begin(), out.techniques.end(), [&] (Technique x, Technique y)
        {
            auto rankOf = [&] (Technique t)
            {
                for (size_t g = 0; g < groups.size(); ++g)
                    if (groups[g].rfind (std::string (toString (t)) + ":", 0) == 0)
                        return g;
                return groups.size();
            };
            return rankOf (x) < rankOf (y);
        });
        for (auto t : out.techniques)
            out.families |= familyBit (familyOf (t));

        if (picks.empty())
        {
            out.title = "Original";
            out.summary = "The progression as played, voiced with the current range, density and bass settings.";
            return out;
        }

        std::vector<std::string> titles;
        for (const auto& g : groups)
        {
            const auto colon = g.find (':');
            const std::string tname = g.substr (0, colon), variant = g.substr (colon + 1);
            for (const auto& [seg, c] : picks)
                if (c->groupKey() == g)
                {
                    titles.push_back (titleFor (c->technique, variant));
                    break;
                }
            (void) tname;
        }
        out.title = titles.empty() ? "Suggestion" : titles.front();
        if (titles.size() > 1)
            out.title += " + " + lowerFirst (titles[1]);

        std::string techText;
        for (size_t k = 0; k < out.techniques.size(); ++k)
        {
            if (k > 0)
                techText += k + 1 == out.techniques.size() ? " and " : ", ";
            techText += toString (out.techniques[k]);
        }
        char move[32];
        std::snprintf (move, sizeof (move), "%.1f", out.movement);
        out.summary = "Changes " + std::to_string (changedCount) + " of " + std::to_string (es.size())
                    + " chords using " + techText + ", with about " + move + " semitones of voice movement per change.";
        return out;
    }

    std::vector<bool> lockedFlags (const std::vector<analysis::ChordSegment>& segs, const std::set<int>& ids)
    {
        std::vector<bool> f (segs.size());
        for (size_t i = 0; i < segs.size(); ++i)
            f[i] = segs[i].userLocked || ids.count (segs[i].id) > 0;
        return f;
    }

    SolverOptions solverOptions (const Settings& s)
    {
        return { s.rangeLow, s.rangeHigh, s.density, s.smoothness };
    }
}

std::vector<Suggestion> generate (const analysis::AnalysisResult& analysis, const Settings& settingsIn,
                                  const std::set<int>& lockedSegmentIds)
{
    const auto s = settingsIn.sanitized();
    const auto segs = CandidateGenerator::sortedSegments (analysis);
    if (segs.empty())
        return {};
    const auto locked = lockedFlags (segs, lockedSegmentIds);

    HeuristicRanker scorer;
    auto cands = CandidateGenerator::generate (analysis, s, lockedSegmentIds, 30.0);
    auto plans = Ranker::rank (cands, segs, analysis.key, s, s.maxSuggestions, scorer);
    if ((int) plans.size() < 6)
    {
        // Too little to choose from under the tension gate: lift it rather than return a thin set.
        cands = CandidateGenerator::generate (analysis, s, lockedSegmentIds, 1000.0);
        plans = Ranker::rank (cands, segs, analysis.key, s, s.maxSuggestions, scorer);
    }

    VoiceLeadingSolver solver (solverOptions (s));
    std::vector<Suggestion> out;
    std::map<std::string, int> titleCount;
    for (const auto& p : plans)
    {
        std::map<int, const Candidate*> picks;
        for (const auto* c : p.picks)
            picks[c->segmentIndex] = c;
        auto sg = build (analysis, s, segs, locked, picks, p.groups, solver);
        sg.score = p.score;

        char id[24];
        std::snprintf (id, sizeof (id), "sg-%016llx", (unsigned long long) hashString (0, p.key));
        sg.id = id;
        const int n = ++titleCount[sg.title];
        if (n > 1)
            sg.title += " (take " + std::to_string (n) + ")";
        out.push_back (std::move (sg));
    }
    return out;
}

Suggestion voiceOriginal (const analysis::AnalysisResult& analysis, const Settings& settingsIn,
                          const std::set<int>& lockedSegmentIds)
{
    const auto s = settingsIn.sanitized();
    const auto segs = CandidateGenerator::sortedSegments (analysis);
    VoiceLeadingSolver solver (solverOptions (s));
    auto sg = build (analysis, s, segs, lockedFlags (segs, lockedSegmentIds), {}, {}, solver);
    sg.id = "original";
    return sg;
}

} // namespace so::reharm
