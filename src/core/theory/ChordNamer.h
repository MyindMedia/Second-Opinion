#pragma once

#include "Chord.h"

#include <optional>
#include <string>
#include <vector>

namespace so::theory
{

struct Key;

/** User-facing naming preferences (FR-SET-01). */
struct NamingOptions
{
    enum class Style { Pop, Jazz };
    enum class Spelling { Auto, Flats, Sharps };

    Style style { Style::Pop };
    bool preferSlash { true };          // G/F over F6sus2♭5, C/A over Am7
    Spelling spelling { Spelling::Auto };
    AccidentalStyle accidentals { AccidentalStyle::Unicode };
};

/** Turns chords into symbols, and pitch-class sets into the most musical chord reading. */
class ChordNamer
{
public:
    /** Symbol for a structured chord, e.g. {D♭, maj7, bass F} → "D♭maj7/F" (Pop) or "D♭Δ7/F" (Jazz).
        Auto spelling keeps the chord's own spelling; Flats/Sharps respell root and bass. */
    static std::string name (const Chord& chord, const NamingOptions& options = {});

    /** Applies the spelling preference to a chord (used before naming and for display). */
    static Chord respell (const Chord& chord, NamingOptions::Spelling spelling, const Key* key = nullptr);

    /** Best reading of a set of upper pitch classes over a bass pitch class.
        `upperPcs` are the notes above the bass (duplicates and order don't matter; the bass may
        or may not be repeated among them).

        With preferSlash on, the upper notes are named as a chord and the bass is written after the
        slash: {G,B,D} over F → G/F (never F6sus2♭5), {G,D,B} over B → G/B.
        With preferSlash off, a root-position name that includes the bass wins when the vocabulary
        has one: {C,E,G} over A → Am7; otherwise it falls back to the slash name.

        Returns all readings ranked best-first (empty when nothing in the vocabulary fits). */
    static std::vector<Chord> readings (const std::vector<int>& upperPcs, int bassPc,
                                        const NamingOptions& options = {}, const Key* key = nullptr);

    static std::optional<Chord> bestReading (const std::vector<int>& upperPcs, int bassPc,
                                             const NamingOptions& options = {}, const Key* key = nullptr);
};

} // namespace so::theory
