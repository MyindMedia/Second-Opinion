#include "Pitch.h"

#include <array>

namespace so::theory
{

int letterPc (Letter l) noexcept
{
    static constexpr std::array<int, 7> pcs { 0, 2, 4, 5, 7, 9, 11 };
    return pcs[(size_t) l];
}

std::string SpelledPitch::toString (AccidentalStyle style) const
{
    static constexpr std::array<char, 7> names { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
    std::string s (1, names[(size_t) letter]);

    const bool uni = style == AccidentalStyle::Unicode;

    if (accidental == 2)        s += uni ? "𝄪" : "##";
    else if (accidental == 1)   s += uni ? "♯" : "#";
    else if (accidental == -1)  s += uni ? "♭" : "b";
    else if (accidental == -2)  s += uni ? "𝄫" : "bb";

    return s;
}

SpelledPitch SpelledPitch::fromPc (int pc, bool preferFlats)
{
    pc = wrapPc (pc);

    switch (pc)
    {
        case 0:  return { Letter::C, 0 };
        case 1:  return preferFlats ? SpelledPitch { Letter::D, -1 } : SpelledPitch { Letter::C, 1 };
        case 2:  return { Letter::D, 0 };
        case 3:  return preferFlats ? SpelledPitch { Letter::E, -1 } : SpelledPitch { Letter::D, 1 };
        case 4:  return { Letter::E, 0 };
        case 5:  return { Letter::F, 0 };
        case 6:  return preferFlats ? SpelledPitch { Letter::G, -1 } : SpelledPitch { Letter::F, 1 };
        case 7:  return { Letter::G, 0 };
        case 8:  return preferFlats ? SpelledPitch { Letter::A, -1 } : SpelledPitch { Letter::G, 1 };
        case 9:  return { Letter::A, 0 };
        case 10: return preferFlats ? SpelledPitch { Letter::B, -1 } : SpelledPitch { Letter::A, 1 };
        default: return { Letter::B, 0 };
    }
}

SpelledPitch SpelledPitch::fromPcAuto (int pc)
{
    return wrapPc (pc) == 6 ? SpelledPitch { Letter::F, 1 } : fromPc (pc, true);
}

SpelledPitch SpelledPitch::above (const SpelledPitch& root, int semitones, int letterSteps)
{
    const auto letter = (Letter) (((int) root.letter + letterSteps) % 7);
    const int target = wrapPc (root.pc() + semitones);
    int acc = wrapPc (target - letterPc (letter));

    if (acc > 6)
        acc -= 12;

    // Beyond double accidentals the letter-based spelling is unreadable, so fall back.
    if (acc < -2 || acc > 2)
        return fromPcAuto (target);

    return { letter, acc };
}

std::optional<std::pair<SpelledPitch, size_t>> parsePitchPrefix (std::string_view text)
{
    if (text.empty())
        return std::nullopt;

    Letter letter {};

    switch (text[0])
    {
        case 'C': case 'c': letter = Letter::C; break;
        case 'D': case 'd': letter = Letter::D; break;
        case 'E': case 'e': letter = Letter::E; break;
        case 'F': case 'f': letter = Letter::F; break;
        case 'G': case 'g': letter = Letter::G; break;
        case 'A': case 'a': letter = Letter::A; break;
        case 'B': case 'b': letter = Letter::B; break;
        default: return std::nullopt;
    }

    size_t i = 1;
    int acc = 0;

    auto startsWith = [&] (std::string_view token) { return text.substr (i).substr (0, token.size()) == token; };

    while (i < text.size() && acc > -2 && acc < 2)
    {
        if (startsWith ("♭"))      { acc -= 1; i += std::string_view ("♭").size(); }
        else if (startsWith ("♯")) { acc += 1; i += std::string_view ("♯").size(); }
        else if (startsWith ("𝄫")) { acc -= 2; i += std::string_view ("𝄫").size(); }
        else if (startsWith ("𝄪")) { acc += 2; i += std::string_view ("𝄪").size(); }
        else if (text[i] == '#')   { acc += 1; ++i; }
        else if (text[i] == 'b')
        {
            // "b" is a flat here, except in "Bb...": still a flat. Never a root letter after a root letter.
            acc -= 1; ++i;
        }
        else break;
    }

    return std::make_pair (SpelledPitch { letter, acc }, i);
}

std::optional<SpelledPitch> parsePitch (std::string_view text)
{
    auto r = parsePitchPrefix (text);

    if (! r || r->second != text.size())
        return std::nullopt;

    return r->first;
}

} // namespace so::theory
