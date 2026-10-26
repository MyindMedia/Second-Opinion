#pragma once

#include "ChordQuality.h"
#include "Pitch.h"

#include <optional>
#include <vector>

namespace so::theory
{

/** A chord as a musician thinks of it: root, quality and an optional bass note.
    The bass is kept exactly as given (G/B stays G/B, G/F stays G/F) because users told us
    "correcting" their slash chords away is a dealbreaker (PRD §3, FR-AN-04). */
struct Chord
{
    SpelledPitch root;
    ChordQuality quality { ChordQuality::Major };
    std::optional<SpelledPitch> bass;   // empty = root position

    /** Pitch classes of the chord tones (root first, in interval order). Excludes a foreign bass. */
    std::vector<int> chordTonePcs() const;

    /** Spelled chord tones (D♭maj7 → D♭ F A♭ C). */
    std::vector<SpelledPitch> chordToneSpellings() const;

    /** All pitch classes that sound, including a foreign bass (G/F → G B D F). */
    std::vector<int> allPcs() const;

    /** Pitch class that sounds lowest. */
    int bassPc() const noexcept { return bass ? bass->pc() : root.pc(); }

    bool isSlash() const noexcept { return bass.has_value() && bass->pc() != root.pc(); }

    /** True when the bass is one of the chord tones (an inversion, e.g. G/B) rather than foreign (G/F). */
    bool bassIsChordTone() const;

    /** Same sounding chord (root pc, quality, bass pc), ignoring spelling. */
    bool sameHarmony (const Chord& o) const noexcept
    {
        return root.pc() == o.root.pc() && quality == o.quality && bassPc() == o.bassPc();
    }

    bool operator== (const Chord&) const = default;
};

} // namespace so::theory
