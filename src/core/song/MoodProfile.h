#pragma once

// The mood a song should have, as numbers the song builder can act on (FR-SB-02).
// Produced offline by MoodInterpreter, or online by an AI model that returns the JSON documented
// in kMoodProfileJsonSchema. Both paths feed the same SongBuilder.

#include "SongTypes.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace so::song
{

/** A partial mood for one section: only the fields that are set override the global profile. */
struct MoodOverride
{
    std::optional<float> valence;
    std::optional<float> energy;
    std::optional<float> tension;
    std::optional<float> darkness;
    std::optional<float> sophistication;
    std::optional<float> chordsPerBar;
    std::optional<std::vector<ColorFamily>> colors;

    bool empty() const noexcept
    {
        return ! valence && ! energy && ! tension && ! darkness && ! sophistication && ! chordsPerBar && ! colors;
    }

    bool operator== (const MoodOverride&) const = default;
};

struct MoodProfile
{
    float valence { 0.0f };          // -1 sad .. +1 happy
    float energy { 0.5f };           // 0 calm .. 1 intense
    float tension { 0.3f };          // 0 settled .. 1 unresolved, dominant-heavy
    float darkness { 0.3f };         // 0 bright .. 1 dark (minor, Phrygian, borrowed)
    float sophistication { 0.4f };   // 0 plain triads .. 1 extended jazz and neo-soul colors
    Style style { Style::Auto };
    float chordsPerBar { 0.0f };     // harmonic rhythm; 0 = style default, else 0.25..4
    std::vector<ColorFamily> colors; // preferred technique families (empty = no preference)
    std::map<Section, MoodOverride> sections;

    /** Global profile with the section's override applied (the result has no section map). */
    MoodProfile resolved (Section section) const;

    /** Clamps every number into its documented range. */
    void clampAll();

    bool prefers (ColorFamily c) const;

    bool operator== (const MoodProfile&) const = default;
};

/** Serializes to the JSON contract (stable key order, 3 decimals). */
std::string toJson (const MoodProfile& profile);

/** Parses the JSON contract. Unknown keys are ignored, missing keys keep their defaults, numbers
    are clamped. Returns nullopt (and sets `error`) for malformed JSON or wrong value types.
    Accepts the document inside a Markdown code fence. */
std::optional<MoodProfile> moodFromJson (std::string_view json, std::string* error = nullptr);

/** JSON Schema (draft 2020-12) for the MoodProfile document. An online AI interpreter must return
    a document that validates against it. */
extern const std::string_view kMoodProfileJsonSchema;

/** System prompt the integrator sends to the AI model. The user message is the free-text
    description (plus selected chips); the model's reply is parsed with moodFromJson. */
std::string moodSystemPrompt();

/** The user message for the AI call: the description plus the selected chips, as plain text. */
std::string moodUserPrompt (std::string_view description, const std::vector<MoodChip>& chips);

} // namespace so::song
