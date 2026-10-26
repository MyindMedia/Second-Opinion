#include "SongTypes.h"

#include <string>

namespace so::song
{

namespace
{
    constexpr std::array<std::string_view, numStyles> styleNames {
        "Auto", "RnB", "NeoSoul", "HipHop", "Trap", "Drill", "Pop", "Gospel", "LoFi", "Cinematic",
        "Rock", "EDM", "Afrobeats", "Latin", "Jazz", "Country" };

    constexpr std::array<std::string_view, numSections> sectionNames {
        "Intro", "Verse", "PreChorus", "Chorus", "PostChorus", "Bridge", "Breakdown", "Outro" };

    constexpr std::array<std::string_view, numMoodChips> chipNames {
        "Dark", "Hopeful", "Melancholic", "Euphoric", "Dreamy", "Tense", "Romantic", "Aggressive",
        "Chill", "Nostalgic", "Cinematic", "Soulful", "Mysterious", "Triumphant", "Bittersweet", "Gritty" };

    constexpr std::array<std::string_view, numColorFamilies> colorNames {
        "Diatonic", "Borrowed", "SecondaryDominants", "TritoneSubs", "ChromaticMediants",
        "Suspensions", "Extensions", "Modal", "Pedal" };

    constexpr std::array<std::string_view, (int) StructurePreset::Count> structureNames {
        "Auto", "Pop", "RnB", "HipHopTrap", "Gospel", "EDM", "Cinematic", "Custom" };

    constexpr std::array<std::string_view, (int) IdeaRole::Count> roleNames {
        "Verse", "Chorus", "Hook", "Loop" };

    /** Lower-case and drop spaces, dashes, underscores and '&' so "R&B", "rnb" and "Lo-Fi" match. */
    std::string squash (std::string_view s)
    {
        std::string out;
        for (char c : s)
        {
            if (c == ' ' || c == '-' || c == '_' || c == '&' || c == '\'')
                continue;
            out += (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
        }
        return out;
    }

    template <typename E, size_t N>
    std::optional<E> lookup (const std::array<std::string_view, N>& names, std::string_view s)
    {
        const auto key = squash (s);
        for (size_t i = 0; i < N; ++i)
            if (squash (names[i]) == key)
                return (E) (int) i;
        return std::nullopt;
    }
}

std::string_view toString (Style s)           { return styleNames[(size_t) s]; }
std::string_view toString (Section s)         { return sectionNames[(size_t) s]; }
std::string_view toString (MoodChip c)        { return chipNames[(size_t) c]; }
std::string_view toString (ColorFamily c)     { return colorNames[(size_t) c]; }
std::string_view toString (StructurePreset p) { return structureNames[(size_t) p]; }
std::string_view toString (IdeaRole r)        { return roleNames[(size_t) r]; }

std::optional<Style> styleFromString (std::string_view s)
{
    const auto k = squash (s);
    if (k == "rb" || k == "rnb" || k == "randb") return Style::RnB;
    if (k == "hiphop" || k == "rap" || k == "boombap") return Style::HipHop;
    if (k == "lofi") return Style::LoFi;
    if (k == "afrobeat" || k == "afro") return Style::Afrobeats;
    return lookup<Style> (styleNames, s);
}

std::optional<Section> sectionFromString (std::string_view s)
{
    const auto k = squash (s);
    if (k == "hook" || k == "drop") return Section::Chorus;
    if (k == "pre" || k == "build" || k == "buildup" || k == "prehook") return Section::PreChorus;
    if (k == "post" || k == "posthook") return Section::PostChorus;
    return lookup<Section> (sectionNames, s);
}

std::optional<MoodChip> moodChipFromString (std::string_view s)      { return lookup<MoodChip> (chipNames, s); }
std::optional<ColorFamily> colorFamilyFromString (std::string_view s) { return lookup<ColorFamily> (colorNames, s); }
std::optional<StructurePreset> structureFromString (std::string_view s) { return lookup<StructurePreset> (structureNames, s); }
std::optional<IdeaRole> ideaRoleFromString (std::string_view s)       { return lookup<IdeaRole> (roleNames, s); }

} // namespace so::song
