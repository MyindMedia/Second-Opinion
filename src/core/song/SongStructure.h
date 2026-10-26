#pragma once

// Song structures (FR-SB-03): ordered sections with bar lengths, from presets or custom lists.

#include "SongTypes.h"

#include <string>
#include <vector>

namespace so::song
{

struct SectionSpec
{
    Section section { Section::Verse };
    int bars { 8 };
    std::string name;   // display base name; empty = default ("Verse", or "Hook"/"Drop"/"Build" by preset)
};

/** The structure the builder uses when the preset is Auto. */
StructurePreset structureForStyle (Style style);

/** Section list for a preset. Custom returns `custom` unchanged (an empty custom list falls back
    to the style's preset). Auto picks by style. Lengths are in bars of 4/4.
    Pop:        Intro4 Verse8 Pre4 Chorus8 Verse8 Pre4 Chorus8 Bridge8 Chorus8 Outro4
    RnB:        Intro4 Verse8 Pre4 Chorus8 Verse8 Pre4 Chorus8 Bridge4 Chorus8 Outro4
    HipHopTrap: Intro4 Hook8 Verse16 Hook8 Verse16 Hook8 Outro4
    Gospel:     Intro4 Verse8 Chorus8 Verse8 Chorus8 Bridge8 (vamp) Chorus8 Outro4
    EDM:        Intro8 Build8 Drop16 Breakdown8 Build8 Drop16 Outro8
    Cinematic:  Intro8 Verse8 Chorus8 Breakdown8 Bridge8 Chorus8 Outro8 */
std::vector<SectionSpec> structureSections (StructurePreset preset, Style style,
                                            const std::vector<SectionSpec>& custom = {});

/** Default display name for a section under a preset ("Hook" for trap choruses, "Drop" for EDM). */
std::string sectionDisplayName (Section section, StructurePreset preset);

} // namespace so::song
