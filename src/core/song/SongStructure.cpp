#include "SongStructure.h"

namespace so::song
{

StructurePreset structureForStyle (Style style)
{
    switch (style)
    {
        case Style::RnB: case Style::NeoSoul: case Style::LoFi: case Style::Jazz: return StructurePreset::RnB;
        case Style::HipHop: case Style::Trap: case Style::Drill:                 return StructurePreset::HipHopTrap;
        case Style::Gospel:                                                       return StructurePreset::Gospel;
        case Style::EDM:                                                          return StructurePreset::EDM;
        case Style::Cinematic:                                                    return StructurePreset::Cinematic;
        case Style::Auto: case Style::Pop: case Style::Rock: case Style::Afrobeats:
        case Style::Latin: case Style::Country: case Style::Count:               break;
    }
    return StructurePreset::Pop;
}

std::string sectionDisplayName (Section section, StructurePreset preset)
{
    if (preset == StructurePreset::HipHopTrap && section == Section::Chorus) return "Hook";
    if (preset == StructurePreset::EDM && section == Section::Chorus) return "Drop";
    if (preset == StructurePreset::EDM && section == Section::PreChorus) return "Build";
    if (preset == StructurePreset::Gospel && section == Section::Bridge) return "Vamp";
    switch (section)
    {
        case Section::Intro:      return "Intro";
        case Section::Verse:      return "Verse";
        case Section::PreChorus:  return "Pre-Chorus";
        case Section::Chorus:     return "Chorus";
        case Section::PostChorus: return "Post-Chorus";
        case Section::Bridge:     return "Bridge";
        case Section::Breakdown:  return "Breakdown";
        case Section::Outro:      return "Outro";
        case Section::Count:      break;
    }
    return "Section";
}

std::vector<SectionSpec> structureSections (StructurePreset preset, Style style, const std::vector<SectionSpec>& custom)
{
    using S = Section;
    if (preset == StructurePreset::Custom)
    {
        if (! custom.empty())
        {
            auto out = custom;
            for (auto& s : out)
            {
                if (s.bars < 1) s.bars = 1;
                if (s.name.empty()) s.name = sectionDisplayName (s.section, preset);
            }
            return out;
        }
        preset = StructurePreset::Auto;
    }
    if (preset == StructurePreset::Auto)
        preset = structureForStyle (style);

    std::vector<std::pair<S, int>> list;
    switch (preset)
    {
        case StructurePreset::Pop:
            list = { { S::Intro, 4 }, { S::Verse, 8 }, { S::PreChorus, 4 }, { S::Chorus, 8 }, { S::Verse, 8 },
                     { S::PreChorus, 4 }, { S::Chorus, 8 }, { S::Bridge, 8 }, { S::Chorus, 8 }, { S::Outro, 4 } };
            break;
        case StructurePreset::RnB:
            list = { { S::Intro, 4 }, { S::Verse, 8 }, { S::PreChorus, 4 }, { S::Chorus, 8 }, { S::Verse, 8 },
                     { S::PreChorus, 4 }, { S::Chorus, 8 }, { S::Bridge, 4 }, { S::Chorus, 8 }, { S::Outro, 4 } };
            break;
        case StructurePreset::HipHopTrap:
            list = { { S::Intro, 4 }, { S::Chorus, 8 }, { S::Verse, 16 }, { S::Chorus, 8 }, { S::Verse, 16 },
                     { S::Chorus, 8 }, { S::Outro, 4 } };
            break;
        case StructurePreset::Gospel:
            list = { { S::Intro, 4 }, { S::Verse, 8 }, { S::Chorus, 8 }, { S::Verse, 8 }, { S::Chorus, 8 },
                     { S::Bridge, 8 }, { S::Chorus, 8 }, { S::Outro, 4 } };
            break;
        case StructurePreset::EDM:
            list = { { S::Intro, 8 }, { S::PreChorus, 8 }, { S::Chorus, 16 }, { S::Breakdown, 8 },
                     { S::PreChorus, 8 }, { S::Chorus, 16 }, { S::Outro, 8 } };
            break;
        case StructurePreset::Cinematic:
            list = { { S::Intro, 8 }, { S::Verse, 8 }, { S::Chorus, 8 }, { S::Breakdown, 8 }, { S::Bridge, 8 },
                     { S::Chorus, 8 }, { S::Outro, 8 } };
            break;
        case StructurePreset::Auto: case StructurePreset::Custom: case StructurePreset::Count:
            break;
    }

    std::vector<SectionSpec> out;
    for (const auto& [s, bars] : list)
        out.push_back ({ s, bars, sectionDisplayName (s, preset) });
    return out;
}

} // namespace so::song
