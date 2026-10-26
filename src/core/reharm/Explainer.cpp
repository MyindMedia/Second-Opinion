#include "Explainer.h"

#include "Harmony.h"
#include "../theory/ChordNamer.h"
#include "../theory/RomanNumeral.h"

#include <algorithm>
#include <cstdlib>

namespace so::reharm
{

using theory::wrapPc;

namespace
{
    std::string nm (const theory::Chord& c) { return theory::ChordNamer::name (c); }

    /** Spells `pc` the way it appears in the chord when it is a chord tone, else by the key. */
    std::string noteName (int pc, const theory::Chord& c, const theory::Key& key)
    {
        for (const auto& sp : c.chordToneSpellings())
            if (sp.pc() == wrapPc (pc))
            {
                using L = theory::Letter;
                const bool awkward = std::abs (sp.accidental) > 1
                                  || (sp.accidental == -1 && (sp.letter == L::F || sp.letter == L::C))
                                  || (sp.accidental == 1 && (sp.letter == L::E || sp.letter == L::B));
                return awkward ? spellRoot (sp.pc(), key).toString() : sp.toString();
            }
        if (c.bass && c.bass->pc() == wrapPc (pc))
            return c.bass->toString();
        return key.spell (wrapPc (pc)).toString();
    }

    std::string lowerFunction (theory::HarmonicFunction f)
    {
        switch (f)
        {
            case theory::HarmonicFunction::Tonic:       return "home (tonic)";
            case theory::HarmonicFunction::Predominant: return "set-up (predominant)";
            case theory::HarmonicFunction::Dominant:    return "tension (dominant)";
            case theory::HarmonicFunction::Other:       break;
        }
        return "color";
    }

    /** Guide-tone motion from `a` into `b`: "E (the 3rd of C7) moves up a half step to F, the root of Fm9." */
    std::vector<std::string> guideTones (const theory::Chord& a, const theory::Chord& b, const theory::Key& key)
    {
        std::vector<std::string> out;
        const auto bp = b.chordTonePcs();
        for (const auto& iv : theory::info (a.quality).intervals)
        {
            if (iv.degree != 3 && iv.degree != 7 && iv.degree != 4)
                continue;
            const int pc = wrapPc (a.root.pc() + iv.semitones);
            int bestPc = -1, bestMove = 99;
            for (int q : bp)
            {
                int d = wrapPc (q - pc);
                if (d > 6) d -= 12;
                if (std::abs (d) < std::abs (bestMove))
                {
                    bestMove = d;
                    bestPc = q;
                }
            }
            if (bestPc < 0 || std::abs (bestMove) > 2)
                continue;

            const std::string from = noteName (pc, a, key) + " (the " + degreeName (a, pc) + " of " + nm (a) + ")";
            std::string s;
            if (bestMove == 0)
                s = from + " stays put and becomes the " + degreeName (b, bestPc) + " of " + nm (b) + ".";
            else
                s = from + " moves " + (bestMove > 0 ? "up" : "down") + (std::abs (bestMove) == 1 ? " a half step" : " a whole step")
                  + " to " + noteName (bestPc, b, key) + ", the " + degreeName (b, bestPc) + " of " + nm (b) + ".";
            out.push_back (std::move (s));
        }
        return out;
    }

    /** Numeral letters of a Roman numeral, keeping accidental glyphs ("♭VImaj7" -> "♭VI"). */
    std::string numeralBody (const std::string& n)
    {
        std::string out;
        for (char ch : n)
        {
            const auto u = (unsigned char) ch;
            if (u >= 0x80 || ch == 'I' || ch == 'V' || ch == 'i' || ch == 'v')
                out += ch;
            else
                break;
        }
        return out;
    }

    std::string sharedToneText (const theory::Chord& a, const theory::Chord& b, const theory::Key& key)
    {
        const auto ap = a.chordTonePcs();
        for (int pc : b.chordTonePcs())
            if (std::find (ap.begin(), ap.end(), pc) != ap.end())
                return noteName (pc, b, key);
        return {};
    }
}

Explanation Explainer::explain (const ExplainContext& c)
{
    Explanation e;
    const auto rn = theory::RomanNumeral::analyze (c.chord, c.key);
    e.function = rn.function;
    e.roman = rn.numeral;
    e.technique = c.technique;

    // A tritone sub is named by where it resolves (subV7/x), even when it also reads as a V7/y.
    if ((c.technique == Technique::TritoneSub || c.technique == Technique::ChromaticApproach) && c.next)
    {
        const bool toTonic = c.next->root.pc() == c.key.tonic.pc();
        const auto target = numeralBody (theory::RomanNumeral::analyze (*c.next, c.key).numeral);
        e.roman = std::string ("subV") + (c.chord.quality == ChordQuality::Dominant9 ? "9" : "7")
                + (toTonic || target.empty() ? std::string() : "/" + target);
        e.function = theory::HarmonicFunction::Dominant;
    }

    const std::string now = nm (c.chord);
    const std::string was = nm (c.original);
    const std::string next = c.next ? nm (*c.next) : std::string ("the next chord");
    const std::string paren = " (" ;

    std::string first;
    switch (c.technique)
    {
        case Technique::Original:
            e.techniqueTerm = "original, " + e.roman;
            first = "Keeps " + now + ", the " + lowerFunction (rn.function) + " chord here (" + e.roman + ").";
            break;
        case Technique::TritoneSub:
            e.techniqueTerm = "tritone substitution, " + e.roman;
            first = "Swaps " + was + " for " + now + ", which slides down a half step into " + next + paren + e.techniqueTerm + ").";
            break;
        case Technique::ChromaticApproach:
            e.techniqueTerm = "chromatic approach, " + e.roman;
            first = "Adds " + now + " at the end of the bar so the harmony slides down a half step into " + next + paren + e.techniqueTerm + ").";
            break;
        case Technique::SecondaryDominant:
            e.techniqueTerm = "secondary dominant, " + e.roman;
            first = (c.inserted ? "Adds " + now + " at the end of the bar" : "Turns " + was + " into " + now)
                  + " to pull hard into " + next + paren + e.techniqueTerm + ").";
            break;
        case Technique::IiVInsertion:
            e.techniqueTerm = "ii-V insertion, " + e.roman;
            first = isDominantShaped (c.chord)
                  ? "Finishes the run-in: " + now + " falls a fifth into " + next + paren + e.techniqueTerm + ")."
                  : "Starts a two-chord run-in to " + next + " on " + now + paren + e.techniqueTerm + ").";
            break;
        case Technique::ModalInterchange:
        {
            e.techniqueTerm = "modal interchange, " + e.roman;
            const auto par = c.key.parallel();
            const bool darker = par.mode == theory::Mode::Minor;
            first = "Borrows " + now + " from " + par.toString() + " for a " + (darker ? "darker" : "brighter")
                  + " shade than " + was + paren + e.techniqueTerm + ").";
            break;
        }
        case Technique::Backdoor:
            e.techniqueTerm = "backdoor dominant, " + e.roman;
            first = (c.inserted ? "Adds " : "Swaps in ") + now + ", a soft back-door approach that lands on " + next
                  + paren + e.techniqueTerm + ").";
            break;
        case Technique::Extension:
            if (c.variant == "simpler")
            {
                e.techniqueTerm = "simplification, " + e.roman;
                first = "Pares " + was + " down to " + now + " for a cleaner sound" + paren + e.techniqueTerm + ").";
            }
            else
            {
                e.techniqueTerm = "extension, " + e.roman;
                first = "Plays " + was + " as " + now + " for a fuller, richer sound" + paren + e.techniqueTerm + ").";
            }
            break;
        case Technique::DiatonicSub:
        {
            e.techniqueTerm = "diatonic " + (c.variant.empty() ? std::string ("common-tone") : c.variant) + " substitute, " + e.roman;
            first = "Replaces " + was + " with " + now + ", which shares " + std::to_string (commonTones (c.chord, c.original))
                  + " notes with it" + paren + e.techniqueTerm + ").";
            break;
        }
        case Technique::ChromaticMediant:
        {
            e.techniqueTerm = "chromatic mediant, " + e.roman;
            const auto shared = sharedToneText (c.original, c.chord, c.key);
            first = "Jumps a third from " + was + " to " + now
                  + (shared.empty() ? std::string() : ", holding " + shared + " in common,") + " for a cinematic lift"
                  + paren + e.techniqueTerm + ").";
            break;
        }
        case Technique::NeoRiemannianP:
            e.techniqueTerm = "Neo-Riemannian P, parallel";
            first = "Flips " + was + " to " + now + " by moving the 3rd a half step" + paren + e.techniqueTerm + ").";
            break;
        case Technique::NeoRiemannianL:
            e.techniqueTerm = "Neo-Riemannian L, leading-tone exchange";
            first = "Turns " + was + " into " + now + " by moving one note a half step" + paren + e.techniqueTerm + ").";
            break;
        case Technique::NeoRiemannianR:
            e.techniqueTerm = "Neo-Riemannian R, relative";
            first = "Turns " + was + " into " + now + " by moving one note a whole step" + paren + e.techniqueTerm + ").";
            break;
    }

    if (c.melodyPitch)
        e.melodyRelation = "The melody " + noteName (*c.melodyPitch, c.chord, c.key) + " is the "
                         + degreeName (c.chord, *c.melodyPitch) + " of " + now + ".";

    if (c.next)
        e.voiceLeadingNotes = guideTones (c.chord, *c.next, c.key);

    e.simple = first;
    if (! e.melodyRelation.empty())
        e.simple += " " + e.melodyRelation;

    e.detailed = e.simple;
    if (! e.voiceLeadingNotes.empty())
        e.detailed += " " + e.voiceLeadingNotes.front();

    return e;
}

} // namespace so::reharm
