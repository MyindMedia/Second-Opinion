#pragma once

#include "Chord.h"

#include <string>
#include <string_view>
#include <variant>

namespace so::theory
{

struct ParseError
{
    std::string message;   // human-readable, shown under the "Type a chord" field
    size_t position { 0 }; // byte offset where parsing stopped
};

using ParseResult = std::variant<Chord, ParseError>;

/** Parses chord symbols typed by users or produced by ChordNamer, in Pop or Jazz spelling.
    Examples: "G/B", "Am7", "F#m7b5", "Csus2", "D♭maj7/F", "C-7", "BbΔ9", "Eø7", "F°7", "G7sus".
    Whitespace around the symbol and around the slash is ignored. */
class ChordSymbolParser
{
public:
    static ParseResult parse (std::string_view symbol);

    /** Convenience: returns the chord or nullopt. */
    static std::optional<Chord> tryParse (std::string_view symbol);
};

} // namespace so::theory
