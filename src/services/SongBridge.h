#pragma once

// Adapter between so::song (song builder + mood engine) and the bridge contract (Build out mode).

#include "core/analysis/AnalysisTypes.h"
#include "core/midi/MidiWriter.h"
#include "core/song/SongBuilder.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>

namespace SongBridge
{
    struct Request
    {
        std::string description;
        std::vector<so::song::MoodChip> chips;
        juce::StringArray moodIds;   // as sent by the UI, echoed back in Song.moods
        so::song::IdeaRole role { so::song::IdeaRole::Verse };
        so::song::StructurePreset structure { so::song::StructurePreset::Auto };
        std::uint64_t seed { 0 };
    };

    struct Meta
    {
        juce::String interpreter { "offline" };   // offline | online
        juce::String interpretedAs;
        juce::StringArray moods;
        std::uint64_t seed { 0 };
    };

    struct BuildResult
    {
        std::shared_ptr<so::song::Song> song;
        Meta meta;
        juce::String error;
    };

    Request parseRequest (const juce::var& request);

    /** Offline mood engine, or `aiJson` (a MoodProfile document from the online AI) when valid. */
    BuildResult build (const std::optional<so::analysis::AnalysisResult>& idea, const Request& request,
                       const std::string& aiJson, double bpm);

    juce::var toJson (const so::song::Song& song, const Meta& meta);

    bool choose (so::song::Song& song, int sectionIndex, int optionIndex);

    struct Arrangement
    {
        std::vector<so::midi::MidiLane> lanes;
        double bpm { 120.0 };
        double lengthBeats { 0.0 };
    };

    /** target "song" (chosen arrangement) or "section" with id "<i>" or "<i>.<option>" (preview an
        option without choosing it). */
    Arrangement lanes (const so::song::Song& song, const juce::String& target, const juce::String& id);

    std::string aiSystemPrompt();
    std::string aiUserPrompt (const Request& request);
}
