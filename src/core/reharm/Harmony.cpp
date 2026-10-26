#include "Harmony.h"

#include "Suggestion.h"
#include "../theory/RomanNumeral.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace so::reharm
{

using theory::wrapPc;
using Q = ChordQuality;

std::string_view toString (Family f) noexcept
{
    switch (f)
    {
        case Family::Diatonic:         return "Diatonic";
        case Family::Borrowed:         return "Borrowed";
        case Family::Secondary:        return "Secondary dominants";
        case Family::TritoneSub:       return "Tritone subs";
        case Family::ChromaticMediant: return "Chromatic mediants";
        case Family::NeoRiemannian:    return "Neo-Riemannian";
    }
    return "Diatonic";
}

Family familyOf (Technique t) noexcept
{
    switch (t)
    {
        case Technique::Original:
        case Technique::Extension:
        case Technique::DiatonicSub:       return Family::Diatonic;
        case Technique::ModalInterchange:
        case Technique::Backdoor:          return Family::Borrowed;
        case Technique::SecondaryDominant:
        case Technique::IiVInsertion:      return Family::Secondary;
        case Technique::TritoneSub:
        case Technique::ChromaticApproach: return Family::TritoneSub;
        case Technique::ChromaticMediant:  return Family::ChromaticMediant;
        case Technique::NeoRiemannianP:
        case Technique::NeoRiemannianL:
        case Technique::NeoRiemannianR:    return Family::NeoRiemannian;
    }
    return Family::Diatonic;
}

std::string_view toString (Technique t) noexcept
{
    switch (t)
    {
        case Technique::Original:          return "original";
        case Technique::Extension:         return "extension";
        case Technique::DiatonicSub:       return "diatonic substitution";
        case Technique::ModalInterchange:  return "modal interchange";
        case Technique::Backdoor:          return "backdoor dominant";
        case Technique::SecondaryDominant: return "secondary dominant";
        case Technique::IiVInsertion:      return "ii-V insertion";
        case Technique::TritoneSub:        return "tritone substitution";
        case Technique::ChromaticApproach: return "chromatic approach chord";
        case Technique::ChromaticMediant:  return "chromatic mediant";
        case Technique::NeoRiemannianP:    return "Neo-Riemannian P";
        case Technique::NeoRiemannianL:    return "Neo-Riemannian L";
        case Technique::NeoRiemannianR:    return "Neo-Riemannian R";
    }
    return "original";
}

// ----------------------------------------------------------------------------------------------

theory::SpelledPitch spellRoot (int pc, const Key& key)
{
    auto s = key.spell (wrapPc (pc));
    const bool awkward = s.accidental > 1 || s.accidental < -1
                      || (s.accidental == 1 && (s.letter == theory::Letter::E || s.letter == theory::Letter::B))
                      || (s.accidental == -1 && (s.letter == theory::Letter::C || s.letter == theory::Letter::F));
    if (awkward)
        s = theory::SpelledPitch::fromPc (wrapPc (pc), key.signature() <= 0);
    return s;
}

Chord makeChord (int rootPc, ChordQuality q, const Key& key)
{
    return Chord { spellRoot (rootPc, key), q, std::nullopt };
}

bool isDominantShaped (const Chord& c) noexcept
{
    return c.quality == Q::Dominant7 || c.quality == Q::Dominant9 || c.quality == Q::Dominant7Sus4;
}

bool isMajorFamily (const Chord& c) noexcept
{
    switch (c.quality)
    {
        case Q::Major: case Q::Six: case Q::Major7: case Q::Add9: case Q::Major9: return true;
        default: return false;
    }
}

bool isMinorFamily (const Chord& c) noexcept
{
    switch (c.quality)
    {
        case Q::Minor: case Q::MinorSix: case Q::Minor7: case Q::Minor9: return true;
        default: return false;
    }
}

Chord tritoneSubstitute (const Chord& dominant, const Key& key)
{
    const auto q = dominant.quality == Q::Dominant9 ? Q::Dominant9 : Q::Dominant7;
    return makeChord (dominant.root.pc() + 6, q, key);
}

Chord secondaryDominantOf (const Chord& target, const Key& key, bool ninth)
{
    return makeChord (target.root.pc() + 7, ninth ? Q::Dominant9 : Q::Dominant7, key);
}

namespace
{
    bool hasSeventh (const Chord& c) noexcept
    {
        for (const auto& iv : theory::info (c.quality).intervals)
            if (iv.degree == 7)
                return true;
        return false;
    }

    Chord triadOrSeventh (int root, bool minor, bool seventh, const Key& key)
    {
        const auto q = minor ? (seventh ? Q::Minor7 : Q::Minor) : (seventh ? Q::Major7 : Q::Major);
        return makeChord (root, q, key);
    }

    std::uint64_t splitmix (std::uint64_t& state) noexcept
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
}

Chord neoP (const Chord& c, const Key& key, bool keepSeventh)
{
    const bool minor = isMinorFamily (c);
    return triadOrSeventh (c.root.pc(), ! minor, keepSeventh && hasSeventh (c), key);
}

Chord neoL (const Chord& c, const Key& key, bool keepSeventh)
{
    const bool minor = isMinorFamily (c);
    const int root = minor ? c.root.pc() + 8 : c.root.pc() + 4;
    return triadOrSeventh (root, ! minor, keepSeventh && hasSeventh (c), key);
}

Chord neoR (const Chord& c, const Key& key, bool keepSeventh)
{
    const bool minor = isMinorFamily (c);
    const int root = minor ? c.root.pc() + 3 : c.root.pc() + 9;
    return triadOrSeventh (root, ! minor, keepSeventh && hasSeventh (c), key);
}

std::vector<Chord> chromaticMediants (const Chord& c, const Key& key)
{
    std::vector<Chord> out;
    if (! isMajorFamily (c) && ! isMinorFamily (c))
        return out;

    for (int off : { 4, 8, 3, 9 })
    {
        auto m = makeChord (c.root.pc() + off, c.quality, key);
        if (theory::RomanNumeral::analyze (m, key).diatonic)
            continue;
        if (commonTones (m, c) < 1)
            continue;
        out.push_back (m);
    }
    return out;
}

Chord diatonicChord (const Key& key, int degree, bool seventh)
{
    const auto& sc = key.scaleSemitones();
    auto at = [&] (int d) { return sc[(size_t) (d % 7)] + (d >= 7 ? 12 : 0); };
    const int r = at (degree);
    const int third = at (degree + 2) - r;
    const int fifth = at (degree + 4) - r;
    const int sev = at (degree + 6) - r;

    Q q = Q::Major;
    if (third == 4 && fifth == 7)      q = seventh ? (sev == 11 ? Q::Major7 : Q::Dominant7) : Q::Major;
    else if (third == 3 && fifth == 7) q = seventh ? Q::Minor7 : Q::Minor;
    else if (third == 3 && fifth == 6) q = seventh ? Q::HalfDiminished7 : Q::Diminished;
    else                               q = Q::Augmented;

    return Chord { key.scale()[(size_t) degree], q, std::nullopt };
}

int commonTones (const Chord& a, const Chord& b)
{
    const auto pa = a.allPcs();
    int n = 0;
    for (int pc : b.allPcs())
        if (std::find (pa.begin(), pa.end(), pc) != pa.end())
            ++n;
    return n;
}

int pcDistance (const Chord& a, const Chord& b)
{
    const auto pa = a.chordTonePcs();
    int total = 0;
    for (int pc : b.chordTonePcs())
    {
        int best = 6;
        for (int q : pa)
        {
            const int d = wrapPc (pc - q);
            best = std::min (best, std::min (d, 12 - d));
        }
        total += best;
    }
    return total;
}

// ----------------------------------------------------------------------------------------------

int chordTension (const Chord& c, const Key& key)
{
    int t = 0;
    switch (c.quality)
    {
        case Q::Major:           t = 4;  break;
        case Q::Minor:           t = 8;  break;
        case Q::Sus2:            t = 14; break;
        case Q::Sus4:            t = 14; break;
        case Q::Six:             t = 16; break;
        case Q::MinorSix:        t = 20; break;
        case Q::Add9:            t = 16; break;
        case Q::Major7:          t = 18; break;
        case Q::Minor7:          t = 16; break;
        case Q::Dominant7:       t = 32; break;
        case Q::Major9:          t = 26; break;
        case Q::Minor9:          t = 24; break;
        case Q::Dominant9:       t = 38; break;
        case Q::Dominant7Sus4:   t = 26; break;
        case Q::HalfDiminished7: t = 40; break;
        case Q::Diminished:      t = 42; break;
        case Q::Diminished7:     t = 52; break;
        case Q::Augmented:       t = 46; break;
        case Q::Count:           break;
    }

    const int leading = wrapPc (key.tonic.pc() + 11);
    for (int pc : c.chordTonePcs())
    {
        if (key.inScale (pc))
            continue;
        t += (key.mode == theory::Mode::Minor && pc == leading) ? 4 : 10;
    }
    if (! key.inScale (c.root.pc()))
        t += 8;
    if (c.isSlash() && ! c.bassIsChordTone())
        t += 6;

    return std::clamp (t, 0, 100);
}

double targetChordTension (int tensionKnob) noexcept
{
    return 4.0 + 0.8 * std::clamp (tensionKnob, 0, 100);
}

// ----------------------------------------------------------------------------------------------

ToneRole classifyTone (const Chord& c, int pc, int t)
{
    pc = wrapPc (pc);
    const auto all = c.allPcs();
    if (std::find (all.begin(), all.end(), pc) != all.end())
        return ToneRole::ChordTone;

    const int rel = wrapPc (pc - c.root.pc());
    auto allow = [&] (std::initializer_list<int> rels, int minKnob)
    {
        return t >= minKnob && std::find (rels.begin(), rels.end(), rel) != rels.end();
    };

    bool ok = false;
    switch (c.quality)
    {
        case Q::Major: case Q::Six: case Q::Major7: case Q::Add9: case Q::Major9:
            ok = allow ({ 2, 9 }, 0) || allow ({ 11 }, 20) || allow ({ 6 }, 55);
            break;
        case Q::Minor: case Q::MinorSix: case Q::Minor7: case Q::Minor9:
            ok = allow ({ 2, 5, 10 }, 0) || allow ({ 9 }, 30)
              || (c.quality != Q::Minor7 && c.quality != Q::Minor9 && allow ({ 11 }, 70));
            break;
        case Q::Dominant7: case Q::Dominant9:
            ok = allow ({ 2, 9 }, 0) || allow ({ 1, 3, 8 }, 35) || allow ({ 6 }, 55);
            break;
        case Q::Sus2: case Q::Sus4: case Q::Dominant7Sus4:
            ok = allow ({ 2, 9, 10 }, 0) || allow ({ 5 }, 0) || allow ({ 11 }, 40);
            break;
        case Q::HalfDiminished7:
            ok = allow ({ 5, 8 }, 0) || allow ({ 2 }, 40);
            break;
        case Q::Diminished: case Q::Diminished7:
            ok = allow ({ 9 }, 0) || allow ({ 2, 5, 8, 11 }, 40);
            break;
        case Q::Augmented:
            ok = allow ({ 2, 10 }, 30) || allow ({ 6 }, 60);
            break;
        case Q::Count:
            break;
    }
    return ok ? ToneRole::Tension : ToneRole::Avoid;
}

bool isStrongBeat (double beat, int beatsPerBar) noexcept
{
    const double r = std::round (beat);
    if (std::abs (beat - r) > 1.0e-6)
        return false;
    const int bpb = std::max (1, beatsPerBar);
    const int inBar = (int) (((long long) r % bpb + bpb) % bpb);
    return bpb % 2 == 0 ? inBar % 2 == 0 : inBar == 0;
}

std::vector<MelodyHit> melodyHits (const std::vector<analysis::MelodyNote>& melody, double start,
                                   double beats, int beatsPerBar)
{
    constexpr double eps = 1.0e-6;
    const double end = start + beats;
    std::vector<MelodyHit> hits;
    for (const auto& n : melody)
    {
        const double ns = n.startBeat;
        const double ne = n.startBeat + n.beats;
        if (ne <= start + eps || ns >= end - eps)
            continue;

        MelodyHit h;
        h.pitch = n.pitch;
        h.beat = std::max (ns, start);
        const double heard = std::min (ne, end) - h.beat;
        // Sustained into the span: heard against the new chord's attack, so it counts as strong.
        h.strong = ns < start - eps ? isStrongBeat (start, beatsPerBar) : isStrongBeat (ns, beatsPerBar);
        h.weight = (h.strong ? 1.0 : 0.35) * std::clamp (heard, 0.25, 2.0);
        hits.push_back (h);
    }
    return hits;
}

double melodyFit (const Chord& c, const std::vector<MelodyHit>& hits, int t, bool& strongOk)
{
    strongOk = true;
    if (hits.empty())
        return 1.0;

    double sum = 0.0, wsum = 0.0;
    for (const auto& h : hits)
    {
        const auto role = classifyTone (c, h.pitch, t);
        double v = 1.0;
        if (role == ToneRole::Tension)
            v = 0.55 + 0.4 * t / 100.0;
        else if (role == ToneRole::Avoid)
        {
            v = 0.1;
            if (h.strong)
                strongOk = false;
        }
        sum += v * h.weight;
        wsum += h.weight;
    }
    return wsum > 0.0 ? sum / wsum : 1.0;
}

std::optional<int> melodyAt (const std::vector<analysis::MelodyNote>& melody, double beat, double window)
{
    constexpr double eps = 1.0e-6;
    std::optional<int> first;
    double firstStart = 1.0e300;
    for (const auto& n : melody)
    {
        if (n.startBeat <= beat + eps && n.startBeat + n.beats > beat + eps)
            return n.pitch;
        if (n.startBeat > beat && n.startBeat < beat + window - eps && n.startBeat < firstStart)
        {
            first = n.pitch;
            firstStart = n.startBeat;
        }
    }
    return first;
}

std::string degreeName (const Chord& c, int pc)
{
    const int rel = wrapPc (pc - c.root.pc());
    const auto fam = theory::info (c.quality).family;
    const bool minorish = fam == theory::QualityFamily::Minor || fam == theory::QualityFamily::Diminished;
    switch (rel)
    {
        case 0:  return "root";
        case 1:  return "flat 9th";
        case 2:  return c.quality == Q::Sus2 ? "2nd" : "9th";
        case 3:  return minorish ? "3rd" : "sharp 9th";
        case 4:  return "3rd";
        case 5:  return (c.quality == Q::Sus4 || c.quality == Q::Dominant7Sus4) ? "4th" : "11th";
        case 6:  return fam == theory::QualityFamily::Diminished ? "flat 5th" : "sharp 11th";
        case 7:  return "5th";
        case 8:  return c.quality == Q::Augmented ? "sharp 5th" : "flat 13th";
        case 9:  return (c.quality == Q::Six || c.quality == Q::MinorSix) ? "6th"
                      : c.quality == Q::Diminished7 ? "7th" : "13th";
        case 10: return "7th";
        default: return "major 7th";
    }
}

// ----------------------------------------------------------------------------------------------

std::uint64_t mix64 (std::uint64_t x) noexcept
{
    std::uint64_t s = x;
    return splitmix (s);
}

std::uint64_t hashString (std::uint64_t seed, const std::string& s) noexcept
{
    std::uint64_t h = 1469598103934665603ull ^ mix64 (seed);
    for (unsigned char ch : s)
    {
        h ^= ch;
        h *= 1099511628211ull;
    }
    return mix64 (h);
}

double unitNoise (std::uint64_t seed, const std::string& key) noexcept
{
    return (double) (hashString (seed, key) >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace so::reharm
