#include "ChordNamer.h"
#include "Key.h"

#include <algorithm>
#include <cstdlib>

namespace so::theory
{

namespace
{
    std::string applyGlyphs (std::string_view suffix, AccidentalStyle acc)
    {
        std::string s (suffix);
        if (acc == AccidentalStyle::Unicode)
        {
            for (size_t i = 0; i < s.size(); ++i)
                if (s[i] == 'b' && i > 0 && s[i - 1] == '7')
                    s.replace (i, 1, "♭");
        }
        return s;
    }

    SpelledPitch spellBassFor (const Chord& chord, int bassPc, const SpelledPitch& fallback)
    {
        const auto& ivs = info (chord.quality).intervals;
        for (const auto& iv : ivs)
            if (wrapPc (chord.root.pc() + iv.semitones) == bassPc)
                return SpelledPitch::above (chord.root, iv.semitones, iv.letterSteps());
        return fallback;
    }

    unsigned maskOf (const std::vector<int>& pcs)
    {
        unsigned m = 0;
        for (int p : pcs) m |= 1u << wrapPc (p);
        return m;
    }

    unsigned rotateMask (unsigned relMask, int root)
    {
        unsigned m = 0;
        for (int i = 0; i < 12; ++i)
            if (relMask & (1u << i))
                m |= 1u << wrapPc (i + root);
        return m;
    }

    /** Auto spelling for a root when no key is given: minor-family chords on the black keys read
        better with sharps (C♯m, F♯m, G♯m), everything else uses the default flat-leaning table. */
    SpelledPitch autoRoot (int pc, ChordQuality q)
    {
        const auto fam = info (q).family;
        const bool minorish = fam == QualityFamily::Minor || fam == QualityFamily::Diminished;
        if (minorish && (pc == 1 || pc == 6 || pc == 8))
            return SpelledPitch::fromPc (pc, false);
        return SpelledPitch::fromPcAuto (pc);
    }
}

Chord ChordNamer::respell (const Chord& chord, NamingOptions::Spelling spelling, const Key* key)
{
    if (spelling == NamingOptions::Spelling::Auto)
    {
        if (key == nullptr)
            return chord;

        Chord c = chord;
        c.root = key->spell (chord.root.pc());
        if (chord.bass)
            c.bass = spellBassFor (c, chord.bass->pc(), key->spell (chord.bass->pc()));
        return c;
    }

    const bool flats = spelling == NamingOptions::Spelling::Flats;
    Chord c = chord;
    c.root = SpelledPitch::fromPc (chord.root.pc(), flats);
    if (chord.bass)
        c.bass = spellBassFor (c, chord.bass->pc(), SpelledPitch::fromPc (chord.bass->pc(), flats));

    // A chord-tone bass spelled from the root can land on a double accidental; prefer the plain spelling.
    if (c.bass && std::abs (c.bass->accidental) > 1)
        c.bass = SpelledPitch::fromPc (c.bass->pc(), flats);

    return c;
}

std::string ChordNamer::name (const Chord& input, const NamingOptions& options)
{
    const Chord chord = respell (input, options.spelling);
    const auto& qi = info (chord.quality);

    std::string s = chord.root.toString (options.accidentals);
    s += applyGlyphs (options.style == NamingOptions::Style::Pop ? qi.pop : qi.jazz, options.accidentals);

    if (chord.isSlash())
        s += "/" + chord.bass->toString (options.accidentals);

    return s;
}

std::vector<Chord> ChordNamer::readings (const std::vector<int>& upperPcs, int bassPc,
                                         const NamingOptions& options, const Key* key)
{
    bassPc = wrapPc (bassPc);
    const unsigned upper = maskOf (upperPcs);
    const unsigned withBass = upper | (1u << bassPc);
    const bool bassInUpper = (upper & (1u << bassPc)) != 0;

    struct Scored { Chord chord; double score; int complexity; };
    std::vector<Scored> found;

    for (int root = 0; root < 12; ++root)
    {
        for (int qi = 0; qi < numChordQualities; ++qi)
        {
            const auto q = (ChordQuality) qi;
            const unsigned m = rotateMask (qualityMask (q), root);

            const bool spellsUpper = m == upper;                       // upper notes alone make this chord
            const bool absorbsBass = ! bassInUpper && m == withBass;   // needs the bass to be complete

            if (! spellsUpper && ! absorbsBass)
                continue;

            Chord c;
            c.quality = q;
            c.root = key != nullptr ? key->spell (root) : autoRoot (root, q);
            if (root != bassPc)
                c.bass = spellBassFor (c, bassPc, key != nullptr ? key->spell (bassPc) : SpelledPitch::fromPcAuto (bassPc));

            double score = info (q).complexity;

            if (root != bassPc)
                score += c.bassIsChordTone() ? 3.0 : 4.0;

            // The G/F rule: with "prefer slash" on, never fold a foreign bass into a fancier chord name.
            if (options.preferSlash && absorbsBass && ! spellsUpper)
                score += 6.0;

            if (key != nullptr)
            {
                bool diatonic = true;
                for (int pc : c.chordTonePcs())
                    diatonic = diatonic && key->inScale (pc);
                if (diatonic)
                    score -= 0.5;
            }

            found.push_back ({ c, score, info (q).complexity });
        }
    }

    std::stable_sort (found.begin(), found.end(), [] (const Scored& a, const Scored& b)
    {
        if (a.score != b.score) return a.score < b.score;
        return a.complexity < b.complexity;
    });

    std::vector<Chord> out;
    for (auto& f : found)
        out.push_back (respell (f.chord, options.spelling, key));
    return out;
}

std::optional<Chord> ChordNamer::bestReading (const std::vector<int>& upperPcs, int bassPc,
                                              const NamingOptions& options, const Key* key)
{
    auto r = readings (upperPcs, bassPc, options, key);
    if (r.empty())
        return std::nullopt;
    return r.front();
}

} // namespace so::theory
