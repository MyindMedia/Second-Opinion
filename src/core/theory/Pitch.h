#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace so::theory
{

/** Wraps any integer into the pitch-class range 0..11. */
constexpr int wrapPc (int v) noexcept { return ((v % 12) + 12) % 12; }

/** Letter names C..B as indices 0..6. */
enum class Letter : int { C = 0, D, E, F, G, A, B };

/** Natural pitch class of a letter (C=0, D=2, E=4, F=5, G=7, A=9, B=11). */
int letterPc (Letter l) noexcept;

/** How accidentals are written out. */
enum class AccidentalStyle { Unicode, Ascii };

/** A spelled pitch class: a letter plus an accidental (-2 = double flat .. +2 = double sharp).
    Spelling matters (D♭ is not C♯) so chord symbols and Roman numerals read the way a musician
    expects. Two pitches are "enharmonic" when their pitch classes match. */
struct SpelledPitch
{
    Letter letter { Letter::C };
    int accidental { 0 };

    int pc() const noexcept { return wrapPc (letterPc (letter) + accidental); }
    bool enharmonic (const SpelledPitch& o) const noexcept { return pc() == o.pc(); }

    std::string toString (AccidentalStyle style = AccidentalStyle::Unicode) const;

    bool operator== (const SpelledPitch&) const = default;

    /** Default spelling for a bare pitch class. preferFlats picks D♭/E♭/G♭/A♭/B♭,
        otherwise C♯/D♯/F♯/G♯/A♯. */
    static SpelledPitch fromPc (int pc, bool preferFlats);

    /** Spelling used when nothing else is known: flats except F♯ (C D♭ D E♭ E F F♯ G A♭ A B♭ B). */
    static SpelledPitch fromPcAuto (int pc);

    /** Spells the note that lies `semitones` above `root` using `letterSteps` letter names
        (e.g. a major third = 4 semitones, 2 letter steps: E♭ → G). */
    static SpelledPitch above (const SpelledPitch& root, int semitones, int letterSteps);
};

/** Parses a pitch name at the start of `text` (e.g. "C", "Bb", "F#", "E♭", "bb").
    Returns the pitch and the number of bytes consumed, or nullopt. */
std::optional<std::pair<SpelledPitch, size_t>> parsePitchPrefix (std::string_view text);

/** Parses a complete pitch name. */
std::optional<SpelledPitch> parsePitch (std::string_view text);

} // namespace so::theory
