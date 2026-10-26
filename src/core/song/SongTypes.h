#pragma once

// Shared vocabulary for the song builder (FR-SB): styles, sections, mood chips, color families.
// Every enum has stable string names used by the JSON contract (docs/adr/0007-song-builder.md).

#include <array>
#include <optional>
#include <string_view>

namespace so::song
{

/** Genre or style tag. Auto lets the builder pick one from the mood. */
enum class Style : int
{
    Auto = 0, RnB, NeoSoul, HipHop, Trap, Drill, Pop, Gospel, LoFi, Cinematic, Rock, EDM,
    Afrobeats, Latin, Jazz, Country, Count
};

/** Song sections. "Hook" is a Chorus with a hip-hop label, "Drop" and "Build" are EDM labels. */
enum class Section : int
{
    Intro = 0, Verse, PreChorus, Chorus, PostChorus, Bridge, Breakdown, Outro, Count
};

/** The 16 mood chips shown in the UI (exactly these, in this order). */
enum class MoodChip : int
{
    Dark = 0, Hopeful, Melancholic, Euphoric, Dreamy, Tense, Romantic, Aggressive, Chill,
    Nostalgic, Cinematic, Soulful, Mysterious, Triumphant, Bittersweet, Gritty, Count
};

/** Harmonic technique families a user can favor (mirrors FR-RH-03 plus song-level colors). */
enum class ColorFamily : int
{
    Diatonic = 0,
    Borrowed,            // modal interchange (iv in major, bVI, bVII)
    SecondaryDominants,
    TritoneSubs,
    ChromaticMediants,
    Suspensions,         // sus2, sus4, 7sus4
    Extensions,          // 7ths, 9ths
    Modal,               // Dorian, Phrygian, Lydian, Mixolydian colors
    Pedal,               // pedal bass under moving chords
    Count
};

/** Structure templates (section order and lengths). */
enum class StructurePreset : int
{
    Auto = 0, Pop, RnB, HipHopTrap, Gospel, EDM, Cinematic, Custom, Count
};

/** Which part of the song the user's idea is. Hook is treated as Chorus. Loop means the idea is
    the backbone that repeats, and the other sections are variations of it. */
enum class IdeaRole : int { Verse = 0, Chorus, Hook, Loop, Count };

constexpr int numStyles = (int) Style::Count;
constexpr int numSections = (int) Section::Count;
constexpr int numMoodChips = (int) MoodChip::Count;
constexpr int numColorFamilies = (int) ColorFamily::Count;

std::string_view toString (Style);
std::string_view toString (Section);
std::string_view toString (MoodChip);
std::string_view toString (ColorFamily);
std::string_view toString (StructurePreset);
std::string_view toString (IdeaRole);

/** Case-insensitive lookups of the names above (also accepts "R&B", "Lo-Fi", "Hook", "Pre"...). */
std::optional<Style> styleFromString (std::string_view);
std::optional<Section> sectionFromString (std::string_view);
std::optional<MoodChip> moodChipFromString (std::string_view);
std::optional<ColorFamily> colorFamilyFromString (std::string_view);
std::optional<StructurePreset> structureFromString (std::string_view);
std::optional<IdeaRole> ideaRoleFromString (std::string_view);

} // namespace so::song
