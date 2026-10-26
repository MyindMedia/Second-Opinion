#pragma once

#include "Pitch.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace so::theory
{

enum class Mode { Major, Minor };

/** A key (tonic + mode). Minor uses natural minor for the scale and spelling; the raised leading
    tone is handled by RomanNumeral analysis. */
struct Key
{
    SpelledPitch tonic;
    Mode mode { Mode::Major };

    /** Semitone offsets of the scale degrees above the tonic. */
    const std::array<int, 7>& scaleSemitones() const noexcept;

    /** Spelled scale notes (F minor → F G A♭ B♭ C D♭ E♭). */
    std::array<SpelledPitch, 7> scale() const;

    bool inScale (int pc) const noexcept;

    /** Scale-degree index 0..6 for an in-scale pitch class. */
    std::optional<int> degreeOf (int pc) const noexcept;

    /** Number of sharps (> 0) or flats (< 0) in the key signature. */
    int signature() const noexcept;

    /** Spells any pitch class to suit this key: scale notes use the scale spelling, others follow
        the signature direction (flat keys and C use flats, sharp keys use sharps). */
    SpelledPitch spell (int pc) const;

    /** The same tonic in the other mode (for modal interchange). */
    Key parallel() const noexcept { return { tonic, mode == Mode::Major ? Mode::Minor : Mode::Major }; }

    /** "F minor", "D♭ major". */
    std::string toString (AccidentalStyle style = AccidentalStyle::Unicode) const;

    /** Parses "F minor", "Fm", "F min", "Bb major", "Bb", "c# minor". */
    static std::optional<Key> parse (std::string_view text);

    bool operator== (const Key&) const = default;
};

} // namespace so::theory
