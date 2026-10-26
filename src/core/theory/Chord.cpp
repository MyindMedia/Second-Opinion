#include "Chord.h"

#include <algorithm>

namespace so::theory
{

std::vector<int> Chord::chordTonePcs() const
{
    std::vector<int> pcs;
    for (const auto& iv : info (quality).intervals)
        pcs.push_back (wrapPc (root.pc() + iv.semitones));
    return pcs;
}

std::vector<SpelledPitch> Chord::chordToneSpellings() const
{
    std::vector<SpelledPitch> out;
    for (const auto& iv : info (quality).intervals)
        out.push_back (SpelledPitch::above (root, iv.semitones, iv.letterSteps()));
    return out;
}

std::vector<int> Chord::allPcs() const
{
    auto pcs = chordTonePcs();
    if (bass && std::find (pcs.begin(), pcs.end(), bass->pc()) == pcs.end())
        pcs.push_back (bass->pc());
    return pcs;
}

bool Chord::bassIsChordTone() const
{
    if (! bass)
        return true;

    const auto pcs = chordTonePcs();
    return std::find (pcs.begin(), pcs.end(), bass->pc()) != pcs.end();
}

} // namespace so::theory
