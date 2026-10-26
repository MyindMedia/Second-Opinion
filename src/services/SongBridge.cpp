#include "SongBridge.h"

#include "bridge/Json.h"
#include "core/song/MoodInterpreter.h"
#include "core/song/MoodProfile.h"
#include "core/song/SongMidi.h"
#include "core/theory/ChordNamer.h"

namespace json = so::bridge::json;

namespace SongBridge
{

Request parseRequest (const juce::var& v)
{
    Request r;
    r.description = v.getProperty ("description", {}).toString().substring (0, 2000).toStdString();
    r.seed = (std::uint64_t) juce::jmax ((juce::int64) 0, (juce::int64) v.getProperty ("seed", 0));

    if (const auto* moods = v.getProperty ("moods", {}).getArray())
        for (const auto& m : *moods)
            if (auto chip = so::song::moodChipFromString (m.toString().toStdString()))
            {
                r.chips.push_back (*chip);
                r.moodIds.add (m.toString().toLowerCase());
            }

    const auto idea = v.getProperty ("ideaSection", "verse").toString().toStdString();
    r.role = so::song::ideaRoleFromString (idea).value_or (so::song::IdeaRole::Verse);

    auto structure = v.getProperty ("structure", "auto").toString();
    if (structure == "hiphop-trap")
        structure = "HipHopTrap";
    r.structure = so::song::structureFromString (structure.toStdString()).value_or (so::song::StructurePreset::Auto);
    return r;
}

static juce::String describe (const so::song::Song& song, const Request& req)
{
    juce::StringArray moodWords;
    for (auto c : req.chips)
        moodWords.add (json::str (std::string (so::song::toString (c))).toLowerCase());

    juce::String s;
    if (! moodWords.isEmpty())
        s << moodWords.joinIntoString (", ") << " ";
    s << json::str (std::string (so::song::toString (song.style)));
    s << " with your idea as the " << json::str (std::string (so::song::toString (req.role))).toLowerCase();
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}

BuildResult build (const std::optional<so::analysis::AnalysisResult>& idea, const Request& req,
                   const std::string& aiJson, double bpm)
{
    BuildResult out;

    so::song::SongRequest sr;
    if (idea)
        sr.idea = *idea;
    sr.ideaRole = req.role;
    sr.structure = req.structure;
    sr.seed = req.seed + 1;
    sr.variationsPerSection = 3;
    sr.bpm = bpm;

    bool online = false;
    if (! aiJson.empty())
    {
        std::string err;
        if (auto profile = so::song::moodFromJson (aiJson, &err))
        {
            sr.mood = *profile;
            online = true;
        }
    }
    if (! online)
        sr.mood = so::song::MoodInterpreter::interpret (req.description, req.chips);

    auto song = std::make_shared<so::song::Song> (so::song::SongBuilder::build (sr));
    if (song->sections.empty())
    {
        out.error = "The song couldn't be built from that description. Try a few different words.";
        return out;
    }

    out.meta.interpreter = online ? "online" : "offline";
    out.meta.interpretedAs = describe (*song, req);
    out.meta.moods = req.moodIds;
    out.meta.seed = req.seed;
    out.song = std::move (song);
    return out;
}

static juce::String sectionKind (const so::song::SongSection& s)
{
    using S = so::song::Section;
    const auto label = json::str (s.label).toLowerCase();
    if (label.startsWith ("hook"))  return "hook";
    if (label.startsWith ("drop"))  return "drop";
    if (label.startsWith ("build")) return "build";
    if (label.startsWith ("vamp"))  return "vamp";
    switch (s.section)
    {
        case S::Intro:      return "intro";
        case S::Verse:      return "verse";
        case S::PreChorus:  return "pre";
        case S::Chorus:     return "chorus";
        case S::PostChorus: return "post";
        case S::Bridge:     return "bridge";
        case S::Breakdown:  return "breakdown";
        case S::Outro:      return "outro";
        case S::Count:      break;
    }
    return "verse";
}

static juce::String structureId (so::song::StructurePreset p)
{
    using P = so::song::StructurePreset;
    switch (p)
    {
        case P::Pop:        return "pop";
        case P::RnB:        return "rnb";
        case P::HipHopTrap: return "hiphop-trap";
        case P::Gospel:     return "gospel";
        case P::EDM:        return "edm";
        case P::Cinematic:  return "cinematic";
        default:            break;
    }
    return "auto";
}

juce::var toJson (const so::song::Song& song, const Meta& meta)
{
    juce::Array<juce::var> sections;

    for (const auto& sec : song.sections)
    {
        juce::Array<juce::var> options;
        for (const auto& opt : sec.options)
        {
            juce::Array<juce::var> chords;
            for (const auto& c : opt.chords)
                chords.add (json::object ({
                    { "startBeat", c.startBeat },
                    { "beats", c.beats },
                    { "symbol", json::symbol (c.chord) },
                    { "roman", json::str (c.roman) },
                    { "fn", json::fn (c.function) },
                }));

            options.add (json::object ({
                { "title", json::str (opt.title) },
                { "explanation", json::str (opt.explanation) },
                { "energy", juce::roundToInt (opt.energy * 100.0f) },
                { "tension", juce::roundToInt (opt.tension * 100.0f) },
                { "chords", chords },
            }));
        }

        sections.add (json::object ({
            { "section", sectionKind (sec) },
            { "label", json::str (sec.label) },
            { "startBar", sec.startBar },
            { "bars", sec.bars },
            { "chosen", sec.chosen },
            { "isIdea", sec.isIdea },
            { "options", options },
        }));
    }

    juce::Array<juce::var> moods;
    for (const auto& m : meta.moods)
        moods.add (m);

    return json::object ({
        { "schemaVersion", 1 },
        { "id", "song-" + juce::String ((juce::int64) meta.seed) },
        { "key", json::keyName (song.key) },
        { "bpm", song.bpm },
        { "beatsPerBar", song.beatsPerBar },
        { "structure", structureId (song.structure) },
        { "moods", moods },
        { "interpreter", meta.interpreter },
        { "interpretedAs", meta.interpretedAs },
        { "arc", json::str (song.arc) },
        { "sections", sections },
    });
}

bool choose (so::song::Song& song, int sectionIndex, int optionIndex)
{
    if (sectionIndex < 0 || sectionIndex >= (int) song.sections.size())
        return false;
    if (optionIndex < 0 || optionIndex >= (int) song.sections[(size_t) sectionIndex].options.size())
        return false;
    so::song::SongBuilder::choose (song, sectionIndex, optionIndex);
    return true;
}

Arrangement lanes (const so::song::Song& song, const juce::String& target, const juce::String& id)
{
    Arrangement a;
    a.bpm = song.bpm;

    if (target == "song")
    {
        a.lanes = so::song::toLanes (so::song::voiceSong (song));
        a.lengthBeats = song.totalBars() * song.beatsPerBar;
        return a;
    }

    const int sectionIndex = id.upToFirstOccurrenceOf (".", false, false).getIntValue();
    if (sectionIndex < 0 || sectionIndex >= (int) song.sections.size())
        return a;

    if (id.containsChar ('.'))
    {
        // Preview another option without choosing it: work on a copy.
        auto copy = song;
        if (choose (copy, sectionIndex, id.fromFirstOccurrenceOf (".", false, false).getIntValue()))
        {
            a.lanes = so::song::toLanes (so::song::voiceSection (copy, sectionIndex));
            a.lengthBeats = copy.sections[(size_t) sectionIndex].bars * copy.beatsPerBar;
        }
        return a;
    }

    a.lanes = so::song::toLanes (so::song::voiceSection (song, sectionIndex));
    a.lengthBeats = song.sections[(size_t) sectionIndex].bars * song.beatsPerBar;
    return a;
}

std::string aiSystemPrompt() { return so::song::moodSystemPrompt(); }

std::string aiUserPrompt (const Request& r) { return so::song::moodUserPrompt (r.description, r.chips); }

} // namespace SongBridge
