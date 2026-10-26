#include "Key.h"

#include <algorithm>
#include <cctype>

namespace so::theory
{

const std::array<int, 7>& Key::scaleSemitones() const noexcept
{
    static constexpr std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    return mode == Mode::Major ? major : minor;
}

std::array<SpelledPitch, 7> Key::scale() const
{
    std::array<SpelledPitch, 7> out;
    const auto& semis = scaleSemitones();
    for (int i = 0; i < 7; ++i)
        out[(size_t) i] = SpelledPitch::above (tonic, semis[(size_t) i], i);
    return out;
}

bool Key::inScale (int pc) const noexcept { return degreeOf (pc).has_value(); }

std::optional<int> Key::degreeOf (int pc) const noexcept
{
    const auto& semis = scaleSemitones();
    const int rel = wrapPc (pc - tonic.pc());
    for (int i = 0; i < 7; ++i)
        if (semis[(size_t) i] == rel)
            return i;
    return std::nullopt;
}

int Key::signature() const noexcept
{
    const auto sc = scale();
    int sharps = 0, flats = 0;
    for (const auto& p : sc)
    {
        if (p.accidental > 0) sharps += p.accidental;
        if (p.accidental < 0) flats -= p.accidental;
    }
    return sharps > 0 ? sharps : -flats;
}

SpelledPitch Key::spell (int pc) const
{
    pc = wrapPc (pc);
    if (auto d = degreeOf (pc))
        return scale()[(size_t) *d];

    return SpelledPitch::fromPc (pc, signature() <= 0);
}

std::string Key::toString (AccidentalStyle style) const
{
    return tonic.toString (style) + (mode == Mode::Major ? " major" : " minor");
}

std::optional<Key> Key::parse (std::string_view text)
{
    while (! text.empty() && text.front() == ' ') text.remove_prefix (1);
    while (! text.empty() && text.back() == ' ')  text.remove_suffix (1);

    auto p = parsePitchPrefix (text);
    if (! p)
        return std::nullopt;

    std::string rest;
    for (char c : text.substr (p->second))
        if (c != ' ')
            rest += (char) std::tolower ((unsigned char) c);

    Key k { p->first, Mode::Major };

    if (rest.empty() || rest == "major" || rest == "maj" || rest == "ionian")
        k.mode = Mode::Major;
    else if (rest == "minor" || rest == "min" || rest == "m" || rest == "aeolian" || rest == "-")
        k.mode = Mode::Minor;
    else
        return std::nullopt;

    return k;
}

} // namespace so::theory
