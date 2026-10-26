#pragma once

// Song builder (FR-SB-04): turns the heard idea plus a mood into a full song of section options.
// Headless, deterministic per seed. See docs/adr/0007-song-builder.md.

#include "../analysis/AnalysisTypes.h"
#include "../theory/Chord.h"
#include "../theory/Key.h"
#include "../theory/RomanNumeral.h"
#include "MoodProfile.h"
#include "SongStructure.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace so::song
{

struct SongChord
{
    double startBeat { 0.0 };   // relative to the section start
    double beats { 4.0 };
    theory::Chord chord;
    std::string roman;          // in the section's key, Unicode accidentals
    theory::HarmonicFunction function { theory::HarmonicFunction::Other };
};

struct SectionOption
{
    std::vector<SongChord> chords;   // as played: body with the transition into the next section applied
    std::vector<SongChord> body;     // before the transition (the builder re-links from this)
    std::string id;                  // stable strategy id, e.g. "minor-relative-lift"
    std::string title;               // e.g. "Lift to the relative major"
    std::string explanation;         // plain language first, theory term in parentheses, <= 3 sentences
    float energy { 0.5f };           // 0..1
    float tension { 0.3f };          // 0..1
    int keyShiftAfter { 0 };         // semitones the sections after this one move up (key change)
    theory::Key bodyKey;             // key `body` is currently spelled in (re-link transposes as needed)

    // Internal: the "why" sentence and color note, kept so explanations can be rebuilt on re-link.
    std::string why;
    std::string colorNote;
};

struct SongSection
{
    Section section { Section::Verse };
    std::string label;               // "Chorus 2", "Hook 1", "Bridge"
    int bars { 8 };
    int startBar { 0 };              // 0-based bar where the section starts in the song
    theory::Key key;                 // the section's key (differs after a key change)
    bool isIdea { false };           // holds the heard chords exactly
    int repeatOf { -1 };             // index of the first section of this type, -1 for a first instance
    bool variation { false };        // a repeat whose chords differ slightly from its source
    std::string variationNote;
    std::vector<SectionOption> options;
    int chosen { 0 };

    const SectionOption& current() const { return options[(size_t) chosen]; }
};

struct Song
{
    theory::Key key;
    double bpm { 120.0 };
    int beatsPerBar { 4 };
    Style style { Style::Auto };     // resolved style (never Auto)
    StructurePreset structure { StructurePreset::Pop };   // resolved preset
    IdeaRole ideaRole { IdeaRole::Verse };
    MoodProfile mood;                // the mood the song was built from (style resolved)
    std::vector<SongSection> sections;
    std::string arc;                 // one paragraph on the overall arc

    int totalBars() const;
};

struct SongRequest
{
    analysis::AnalysisResult idea;           // the heard idea; segments may be empty
    IdeaRole ideaRole { IdeaRole::Verse };
    MoodProfile mood;
    StructurePreset structure { StructurePreset::Auto };
    std::vector<SectionSpec> customStructure;   // used when structure == Custom
    std::uint64_t seed { 1 };
    int variationsPerSection { 3 };          // 1..6
    std::optional<double> bpm;               // host tempo; unset = the idea's tempo, or the style's typical tempo without an idea
};

/** How one chord leads into the next (strongest relation first). */
enum class Transition
{
    None,
    CommonTone,     // shares at least one note
    Stepwise,       // the bass moves by a half or whole step
    Plagal,         // root a fourth above the target (IV to I)
    Backdoor,       // root a whole step below the target (bVII to I)
    HalfStepAbove,  // root a half step above the target (tritone sub, Phrygian bII)
    Dominant        // root a fifth above the target (V to I)
};

std::string_view toString (Transition);

/** The strongest relation from `from` into `to`. */
Transition transitionBetween (const theory::Chord& from, const theory::Chord& to);

/** True when the move is a cadence-like approach (Dominant, HalfStepAbove, Backdoor or Plagal). */
bool isStrongTransition (Transition t);

class SongBuilder
{
public:
    static Song build (const SongRequest& request);

    /** Picks an option for a section and every repeat of it, then re-links transitions. */
    static void choose (Song& song, int sectionIndex, int optionIndex);

    /** Recomputes repeats, keys after key changes, transitions, Roman numerals, explanations and
        the arc from each section's chosen option. Call after editing options by hand. */
    static void relink (Song& song);
};

} // namespace so::song
