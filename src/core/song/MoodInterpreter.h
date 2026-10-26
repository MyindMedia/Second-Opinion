#pragma once

// Offline mood interpreter (FR-SB-02): free text plus mood chips -> MoodProfile.
// Lexicon based and deterministic. See docs/adr/0007-song-builder.md for the design.

#include "MoodProfile.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace so::song
{

/** One lexicon hit, for the UI's "what I heard" line and for debugging. */
struct MatchedTerm
{
    std::string phrase;               // lexicon phrase as matched ("late night")
    std::optional<Section> section;   // bound section, empty = whole song
    float weight { 1.0f };            // intensity multiplier (negative when negated)
    bool negated { false };
    bool fromChip { false };
};

struct MoodReading
{
    MoodProfile profile;
    std::vector<MatchedTerm> terms;
};

class MoodInterpreter
{
public:
    /** Reads a description such as "dark late-night R&B, hopeful chorus, bridge that feels like
        sunrise" plus selected chips. Handles synonyms and slang, negation ("not too dark"),
        intensity ("very", "slightly") and section binding (words near "verse", "chorus", "hook",
        "pre", "bridge", "intro", "outro" apply to that section). */
    static MoodReading read (std::string_view description, const std::vector<MoodChip>& chips = {});

    static MoodProfile interpret (std::string_view description, const std::vector<MoodChip>& chips = {})
    {
        return read (description, chips).profile;
    }

    /** Number of phrases in the lexicon (for tests and the ADR). */
    static size_t lexiconSize();
};

} // namespace so::song
