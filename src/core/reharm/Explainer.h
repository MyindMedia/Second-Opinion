#pragma once

// P4-04: "Why this works" (FR-EX-01/02). Structured fields first, then template text.

#include "../theory/Chord.h"
#include "../theory/Key.h"
#include "Suggestion.h"

#include <optional>
#include <string>

namespace so::reharm
{

struct ExplainContext
{
    theory::Chord chord;                  // the chord being explained
    theory::Chord original;               // what the segment had before
    std::optional<theory::Chord> next;    // the chord that follows
    theory::Key key;
    Technique technique { Technique::Original };
    std::string variant;                  // candidate variant ("richer", "relative", ...)
    bool inserted { false };              // added inside the segment rather than replacing it
    std::optional<int> melodyPitch;
};

class Explainer
{
public:
    static Explanation explain (const ExplainContext& ctx);

    static const std::string& text (const Explanation& e, Depth depth) noexcept
    {
        return depth == Depth::Simple ? e.simple : e.detailed;
    }
};

} // namespace so::reharm
