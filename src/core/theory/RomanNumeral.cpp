#include "RomanNumeral.h"

#include <array>

namespace so::theory
{

std::string_view toString (HarmonicFunction f)
{
    switch (f)
    {
        case HarmonicFunction::Tonic:       return "Tonic";
        case HarmonicFunction::Predominant: return "Predominant";
        case HarmonicFunction::Dominant:    return "Dominant";
        case HarmonicFunction::Other:       break;
    }
    return "Color";
}

std::string RomanNumeral::technique() const
{
    if (diatonic)   return "Diatonic";
    if (borrowed)   return "Modal interchange";
    if (secondary)  return "Secondary dominant";
    if (tritoneSub) return "Tritone substitution";
    return "Chromatic";
}

namespace
{
    std::string asciiFy (std::string s)
    {
        auto rep = [&] (std::string_view from, std::string_view to)
        {
            for (size_t p = s.find (from); p != std::string::npos; p = s.find (from, p + to.size()))
                s.replace (p, from.size(), to);
        };
        rep ("♭", "b");
        rep ("♯", "#");
        return s;
    }

    std::string toLower (std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = (char) (c - 'A' + 'a');
        return s;
    }

    /** Roman numeral body (no quality suffix) for a root `rel` semitones above the tonic. */
    std::string numeralBody (int rel, Mode mode, bool lower, bool diminished)
    {
        static const std::array<std::string_view, 12> majorNames { "I", "♭II", "II", "♭III", "III", "IV", "♭V", "V", "♭VI", "VI", "♭VII", "VII" };
        static const std::array<std::string_view, 12> minorNames { "I", "♭II", "II", "III", "♯III", "IV", "♭V", "V", "VI", "♯VI", "VII", "VII" };

        std::string s (mode == Mode::Major ? majorNames[(size_t) rel] : minorNames[(size_t) rel]);
        if (rel == 6 && diminished)
            s = "♯IV";

        if (lower)
        {
            // Lower-case the numeral letters only, keeping the accidental glyph intact.
            const auto firstLetter = s.find_first_of ("IV");
            s = s.substr (0, firstLetter) + toLower (s.substr (firstLetter));
        }
        return s;
    }

    /** Diatonic triad numeral for scale degree d (used as the "/x" target of secondary chords). */
    std::string degreeNumeral (int d, Mode mode)
    {
        static const std::array<std::string_view, 7> major { "I", "ii", "iii", "IV", "V", "vi", "vii" };
        static const std::array<std::string_view, 7> minor { "i", "ii", "III", "iv", "v", "VI", "VII" };
        return std::string (mode == Mode::Major ? major[(size_t) d] : minor[(size_t) d]);
    }

    bool allIn (const Chord& c, const Key& k, bool allowLeadingTone)
    {
        const int lt = wrapPc (k.tonic.pc() + 11);
        for (int pc : c.chordTonePcs())
            if (! k.inScale (pc) && ! (allowLeadingTone && pc == lt))
                return false;
        return true;
    }

    HarmonicFunction diatonicFunction (int rel, Mode mode)
    {
        using H = HarmonicFunction;
        if (mode == Mode::Major)
        {
            switch (rel) { case 0: case 4: case 9: return H::Tonic;
                           case 2: case 5:         return H::Predominant;
                           case 7: case 11:        return H::Dominant; }
        }
        else
        {
            switch (rel) { case 0: case 3:                 return H::Tonic;
                           case 2: case 5: case 8:         return H::Predominant;
                           case 7: case 10: case 11:       return H::Dominant; }
        }
        return H::Other;
    }

    HarmonicFunction borrowedFunction (int rel)
    {
        using H = HarmonicFunction;
        switch (rel)
        {
            case 0: case 3: case 4: case 9: return H::Tonic;
            case 1: case 2: case 5: case 8: return H::Predominant;
            case 7: case 10: case 11:       return H::Dominant;
        }
        return H::Other;
    }
}

RomanNumeral RomanNumeral::analyze (const Chord& chord, const Key& key, AccidentalStyle style)
{
    RomanNumeral rn;

    const auto& qi = info (chord.quality);
    const int rel = wrapPc (chord.root.pc() - key.tonic.pc());
    const bool lower = qi.family == QualityFamily::Minor || qi.family == QualityFamily::Diminished;
    const bool dim = qi.family == QualityFamily::Diminished;
    const bool dominantShape = chord.quality == ChordQuality::Major
                            || chord.quality == ChordQuality::Dominant7
                            || chord.quality == ChordQuality::Dominant9;

    std::string body = numeralBody (rel, key.mode, lower, dim);
    std::string suffix (qi.roman);

    // In minor, V, V7 and vii°(7) use the raised leading tone (harmonic minor) and count as diatonic.
    const bool minorDominant = key.mode == Mode::Minor && (rel == 7 || rel == 11);
    rn.diatonic = allIn (chord, key, minorDominant);

    if (rn.diatonic)
    {
        rn.function = diatonicFunction (rel, key.mode);
    }
    else if (allIn (chord, key.parallel(), false))
    {
        rn.borrowed = true;
        rn.function = borrowedFunction (rel);
    }
    else if (dominantShape)
    {
        const int targetRel = wrapPc (rel + 5);   // resolves down a fifth
        const int halfStepTarget = wrapPc (rel - 1);
        const auto targetDeg = key.degreeOf (wrapPc (key.tonic.pc() + targetRel));
        const auto subDeg = key.degreeOf (wrapPc (key.tonic.pc() + halfStepTarget));

        if (targetDeg && *targetDeg != 0 && ! (key.mode == Mode::Major && *targetDeg == 6))
        {
            rn.secondary = true;
            rn.function = HarmonicFunction::Dominant;
            body = "V";
            suffix = std::string (qi.roman) + "/" + degreeNumeral (*targetDeg, key.mode);
        }
        else if (chord.quality != ChordQuality::Major && subDeg)
        {
            rn.tritoneSub = true;
            rn.function = HarmonicFunction::Dominant;
            body = "subV";
            suffix = std::string (qi.roman) + (*subDeg == 0 ? "" : "/" + degreeNumeral (*subDeg, key.mode));
        }
    }

    rn.numeral = body + suffix;

    if (style == AccidentalStyle::Ascii)
        rn.numeral = asciiFy (rn.numeral);

    return rn;
}

} // namespace so::theory
