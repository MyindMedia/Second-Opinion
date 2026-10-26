#include "CandidateGenerator.h"

#include "Harmony.h"
#include "../theory/ChordNamer.h"
#include "../theory/RomanNumeral.h"

#include <algorithm>
#include <cmath>

namespace so::reharm
{

using theory::wrapPc;
using Q = ChordQuality;

std::string Candidate::groupKey() const
{
    return std::string (toString (technique)) + ":" + variant;
}

namespace
{
    /** "bVImaj7" -> "bVI", "V7/ii" -> "V/ii", "subV7" -> "subV", "iiø7" -> "ii". */
    std::string romanBody (const std::string& numeral)
    {
        auto bodyOf = [] (const std::string& s)
        {
            size_t i = 0;
            std::string out;
            if (s.rfind ("sub", 0) == 0)
            {
                out = "sub";
                i = 3;
            }
            while (i < s.size() && (s[i] == 'b' || s[i] == '#'))
                out += s[i++];
            while (i < s.size() && (s[i] == 'I' || s[i] == 'V' || s[i] == 'i' || s[i] == 'v'))
                out += s[i++];
            return out;
        };

        const auto slash = numeral.find ('/');
        if (slash == std::string::npos)
            return bodyOf (numeral);
        return bodyOf (numeral.substr (0, slash)) + "/" + bodyOf (numeral.substr (slash + 1));
    }

    std::string asciiRoman (const Chord& c, const Key& k)
    {
        return theory::RomanNumeral::analyze (c, k, theory::AccidentalStyle::Ascii).numeral;
    }

    struct Gen
    {
        const analysis::AnalysisResult& a;
        const Settings& s;
        const Key& key;
        const std::vector<analysis::ChordSegment>& segs;
        const std::set<int>& locked;
        double target;
        double slack;
        int bpb;
        std::vector<Candidate> out;
        std::set<std::string> seen;

        int nextOf (size_t i) const
        {
            if (segs.size() < 2)
                return -1;
            return (int) ((i + 1) % segs.size());
        }

        void add (size_t i, Technique tech, std::string variant, std::vector<Span> spans, int targetSeg, bool insertion)
        {
            const auto& seg = segs[i];
            const auto& orig = seg.chord;

            if (orig.isSlash())
            {
                // Explicit slash basses are user intent: replacements keep the bass, insertions are off.
                if (insertion)
                    return;
                for (auto& sp : spans)
                {
                    sp.chord.bass.reset();
                    const int bpc = orig.bass->pc();
                    if (bpc == sp.chord.root.pc())
                        continue;
                    if (classifyTone (sp.chord, bpc, s.tension) == ToneRole::Avoid)
                        return;
                    sp.chord.bass = orig.bass;
                }
            }

            double fitSum = 0.0, tenSum = 0.0, changed = 0.0;
            std::string sig = std::string (toString (tech)) + "|" + std::to_string (i);
            for (const auto& sp : spans)
            {
                sig += "|" + theory::ChordNamer::name (sp.chord) + "@" + std::to_string (sp.beats);
                if (sp.chord.sameHarmony (orig) && sp.chord.bassPc() == orig.bassPc())
                    continue;

                bool strongOk = true;
                const auto hits = melodyHits (a.melody, sp.startBeat, sp.beats, bpb);
                const double fit = melodyFit (sp.chord, hits, s.tension, strongOk);
                if (! strongOk)
                    return;
                fitSum += fit * sp.beats;
                tenSum += chordTension (sp.chord, key) * sp.beats;
                changed += sp.beats;
            }
            if (changed <= 0.0 || ! seen.insert (sig).second)
                return;

            Candidate c;
            c.segmentIndex = (int) i;
            c.technique = tech;
            c.variant = std::move (variant);
            c.spans = std::move (spans);
            c.targetSegment = targetSeg;
            c.insertion = insertion;
            c.melodyFit = fitSum / changed;
            c.tension = tenSum / changed;
            c.changedBeats = changed;

            if (c.tension > target + slack)
                return;

            if (insertion)
                c.functionFit = 0.8;
            else
            {
                const auto& first = c.spans.front().chord;
                const double ct = std::min (1.0, commonTones (first, orig) / 3.0);
                const auto fa = theory::RomanNumeral::analyze (first, key).function;
                const auto fo = theory::RomanNumeral::analyze (orig, key).function;
                c.functionFit = std::min (1.0, 0.6 * ct + (fa == fo ? 0.4 : 0.0));
            }

            const double tensionMatch = 1.0 - std::min (1.0, std::abs (c.tension - target) / 80.0);
            c.score = 3.0 * c.melodyFit + 2.0 * tensionMatch + c.functionFit
                    + 0.3 * unitNoise (s.seed, sig);
            out.push_back (std::move (c));
        }

        Span whole (size_t i, const Chord& c) const { return { segs[i].startBeat, segs[i].beats, c }; }

        /** Inserting splits the segment; keep at least 2 beats each side unless Tension is high, never
            split a slash chord, and never insert before a chord on the same root (a sus resolving). */
        bool canInsert (size_t i, int targetSeg) const
        {
            const double minBeats = s.tension >= 75 ? 2.0 : 4.0;
            if (segs[i].beats < minBeats - 1.0e-9 || segs[i].chord.isSlash())
                return false;
            return targetSeg < 0 || segs[(size_t) targetSeg].chord.root.pc() != segs[i].chord.root.pc();
        }

        void insertBefore (size_t i, Technique tech, const std::string& variant, const Chord& c, int targetSeg)
        {
            if (! canInsert (i, targetSeg))
                return;
            const auto& seg = segs[i];
            const double d = seg.beats >= 4.0 ? 2.0 : seg.beats / 2.0;
            add (i, tech, variant,
                 { { seg.startBeat, seg.beats - d, seg.chord }, { seg.startBeat + seg.beats - d, d, c } },
                 targetSeg, true);
        }

        // ------------------------------------------------------------------------------------

        bool tonesFit (const Chord& c, const Chord& orig) const
        {
            const auto origPcs = orig.allPcs();
            const bool dominantRoot = wrapPc (c.root.pc() - key.tonic.pc()) == 7;
            const int leading = wrapPc (key.tonic.pc() + 11);
            for (int pc : c.chordTonePcs())
            {
                if (key.inScale (pc) || std::find (origPcs.begin(), origPcs.end(), pc) != origPcs.end())
                    continue;
                if (key.mode == theory::Mode::Minor && dominantRoot && pc == leading)
                    continue;
                return false;
            }
            return true;
        }

        void diatonic (size_t i)
        {
            const auto& orig = segs[i].chord;

            // Extensions and simplifications on the same root.
            std::vector<Q> palette;
            if (isMajorFamily (orig))      palette = { Q::Major, Q::Major7, Q::Six, Q::Add9, Q::Major9, Q::Sus2 };
            else if (isMinorFamily (orig)) palette = { Q::Minor, Q::Minor7, Q::Minor9, Q::MinorSix };
            else if (orig.quality == Q::Dominant7 || orig.quality == Q::Dominant9)
                palette = { Q::Dominant7, Q::Dominant9, Q::Dominant7Sus4, Q::Major };
            else if (orig.quality == Q::Sus2 || orig.quality == Q::Sus4 || orig.quality == Q::Dominant7Sus4)
                palette = { Q::Sus4, Q::Dominant7Sus4, Q::Sus2, Q::Major, Q::Minor, Q::Dominant7 };
            else if (orig.quality == Q::HalfDiminished7 || orig.quality == Q::Diminished)
                palette = { Q::HalfDiminished7, Q::Diminished };

            const int origT = chordTension (orig, key);
            for (auto q : palette)
            {
                if (q == orig.quality)
                    continue;
                Chord c { orig.root, q, std::nullopt };
                if (! tonesFit (c, orig))
                    continue;
                add (i, Technique::Extension, chordTension (c, key) > origT ? "richer" : "simpler",
                     { whole (i, c) }, -1, false);
            }

            // Diatonic substitutes sharing two triad tones (relative, mediant).
            const auto origCore = orig.chordTonePcs();
            for (int d = 0; d < 7; ++d)
            {
                for (bool seventh : { false, true })
                {
                    const auto c = diatonicChord (key, d, seventh);
                    if (c.root.pc() == orig.root.pc())
                        continue;
                    if ((c.quality == Q::Diminished || c.quality == Q::HalfDiminished7) && s.tension < 50)
                        continue;
                    if (c.quality == Q::Augmented)
                        continue;
                    int shared = 0;
                    const auto pcs = c.chordTonePcs();
                    for (size_t k = 0; k < std::min<size_t> (3, origCore.size()); ++k)
                        if (std::find (pcs.begin(), pcs.begin() + 3, origCore[k]) != pcs.begin() + 3)
                            ++shared;
                    if (shared < 2)
                        continue;
                    const int rel = wrapPc (c.root.pc() - orig.root.pc());
                    const std::string v = (rel == 3 || rel == 9) ? "relative" : (rel == 4 || rel == 8) ? "mediant" : "common-tone";
                    add (i, Technique::DiatonicSub, v, { whole (i, c) }, -1, false);
                }
            }
        }

        void borrowed (size_t i)
        {
            const auto& orig = segs[i].chord;
            const auto par = key.parallel();
            const auto fo = theory::RomanNumeral::analyze (orig, key).function;

            std::vector<Chord> pool;
            for (int d = 0; d < 7; ++d)
                for (bool seventh : { false, true })
                    pool.push_back (diatonicChord (par, d, seventh));
            if (key.mode == theory::Mode::Major)
                pool.push_back (Chord { par.scale()[3], Q::MinorSix, std::nullopt });   // iv6

            for (const auto& c : pool)
            {
                if (c.quality == Q::Diminished || c.quality == Q::Augmented || isDominantShaped (c))
                    continue;
                const auto rn = theory::RomanNumeral::analyze (c, key);
                if (rn.diatonic || ! rn.borrowed)
                    continue;
                const int ct = commonTones (c, orig);
                if (ct < 1 || (ct < 2 && rn.function != fo))
                    continue;
                add (i, Technique::ModalInterchange, romanBody (asciiRoman (c, key)), { whole (i, c) }, -1, false);
            }

            // Backdoor bVII7 into the tonic.
            const int j = nextOf (i);
            if (j >= 0)
            {
                const auto& tgt = segs[(size_t) j].chord;
                if (tgt.root.pc() == key.tonic.pc() && (isMajorFamily (tgt) || isMinorFamily (tgt)))
                {
                    const auto bd = makeChord (key.tonic.pc() + 10, Q::Dominant7, key);
                    if (bd.sameHarmony (orig))
                        return;
                    if (isDominantShaped (orig) || fo == theory::HarmonicFunction::Dominant
                        || fo == theory::HarmonicFunction::Predominant)
                        add (i, Technique::Backdoor, "bVII7", { whole (i, bd) }, j, false);
                    else
                        insertBefore (i, Technique::Backdoor, "bVII7", bd, j);
                }
            }
        }

        void secondary (size_t i)
        {
            const int j = nextOf (i);
            if (j < 0)
                return;
            const auto& orig = segs[i].chord;
            const auto& tgt = segs[(size_t) j].chord;
            const auto tfam = theory::info (tgt.quality).family;
            if (tfam == theory::QualityFamily::Diminished || tfam == theory::QualityFamily::Augmented)
                return;

            const auto sd = secondaryDominantOf (tgt, key, s.tension >= 60);
            const std::string v = romanBody (asciiRoman (sd, key));

            if (orig.root.pc() == sd.root.pc())
            {
                if (orig.quality != Q::Dominant7 && orig.quality != Q::Dominant9)
                    add (i, Technique::SecondaryDominant, v, { whole (i, sd) }, j, false);
            }
            else
            {
                insertBefore (i, Technique::SecondaryDominant, v, sd, j);
                if (segs[i].beats <= 2.0 + 1.0e-9)
                    add (i, Technique::SecondaryDominant, v, { whole (i, sd) }, j, false);
            }

            // ii-V into the target.
            if (segs[i].beats >= 2.0 - 1.0e-9 && ! orig.isSlash())
            {
                const bool minorTarget = tfam == theory::QualityFamily::Minor;
                const auto ii = makeChord (tgt.root.pc() + 2, minorTarget ? Q::HalfDiminished7 : Q::Minor7, key);
                const auto v7 = makeChord (tgt.root.pc() + 7, Q::Dominant7, key);
                const double h = segs[i].beats / 2.0;
                add (i, Technique::IiVInsertion, "ii-" + romanBody (asciiRoman (v7, key)),
                     { { segs[i].startBeat, h, ii }, { segs[i].startBeat + h, h, v7 } }, j, false);
            }
        }

        void tritone (size_t i)
        {
            const int j = nextOf (i);
            if (j < 0)
                return;
            const auto& orig = segs[i].chord;
            const auto& tgt = segs[(size_t) j].chord;
            const int tr = tgt.root.pc();

            const bool dominantLike = isDominantShaped (orig)
                                   || (orig.quality == Q::Major && wrapPc (orig.root.pc() - tr) == 7);
            if (dominantLike && wrapPc (orig.root.pc() - tr) == 7)
            {
                auto sub = tritoneSubstitute (orig, key);
                if (s.tension >= 60)
                    sub.quality = Q::Dominant9;
                add (i, Technique::TritoneSub, "subV", { whole (i, sub) }, j, false);
            }

            if (orig.root.pc() != wrapPc (tr + 1))
                insertBefore (i, Technique::ChromaticApproach, "subV", makeChord (tr + 1, Q::Dominant7, key), j);
        }

        void mediants (size_t i)
        {
            for (const auto& c : chromaticMediants (segs[i].chord, key))
                add (i, Technique::ChromaticMediant, romanBody (asciiRoman (c, key)), { whole (i, c) }, -1, false);
        }

        void neoRiemannian (size_t i)
        {
            const auto& orig = segs[i].chord;
            if (! isMajorFamily (orig) && ! isMinorFamily (orig))
                return;
            for (bool seventh : { false, true })
            {
                add (i, Technique::NeoRiemannianP, "P", { whole (i, neoP (orig, key, seventh)) }, -1, false);
                add (i, Technique::NeoRiemannianL, "L", { whole (i, neoL (orig, key, seventh)) }, -1, false);
                add (i, Technique::NeoRiemannianR, "R", { whole (i, neoR (orig, key, seventh)) }, -1, false);
            }
        }
    };
}

std::vector<analysis::ChordSegment> CandidateGenerator::sortedSegments (const analysis::AnalysisResult& a)
{
    auto segs = a.segments;
    std::stable_sort (segs.begin(), segs.end(),
                      [] (const auto& x, const auto& y) { return x.startBeat < y.startBeat; });
    segs.erase (std::remove_if (segs.begin(), segs.end(), [] (const auto& sg) { return sg.beats <= 0.0; }), segs.end());
    return segs;
}

std::vector<Candidate> CandidateGenerator::generate (const analysis::AnalysisResult& analysis, const Settings& settingsIn,
                                                     const std::set<int>& lockedSegmentIds, double gateSlack)
{
    const auto s = settingsIn.sanitized();
    const auto segs = sortedSegments (analysis);

    Gen g { analysis, s, analysis.key, segs, lockedSegmentIds, targetChordTension (s.tension), gateSlack,
            std::max (1, analysis.grid.beatsPerBar), {}, {} };

    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].userLocked || lockedSegmentIds.count (segs[i].id) > 0)
            continue;
        if (s.enabled (Family::Diatonic))         g.diatonic (i);
        if (s.enabled (Family::Borrowed))         g.borrowed (i);
        if (s.enabled (Family::Secondary))        g.secondary (i);
        if (s.enabled (Family::TritoneSub))       g.tritone (i);
        if (s.enabled (Family::ChromaticMediant)) g.mediants (i);
        if (s.enabled (Family::NeoRiemannian))    g.neoRiemannian (i);
    }
    return std::move (g.out);
}

} // namespace so::reharm
