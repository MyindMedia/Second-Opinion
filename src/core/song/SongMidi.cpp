#include "SongMidi.h"

namespace so::song
{

std::vector<midi::TimedChord> timedChords (const Song& song, std::optional<int> sectionIndex)
{
    std::vector<midi::TimedChord> out;
    const double bpb = (double) song.beatsPerBar;
    for (size_t i = 0; i < song.sections.size(); ++i)
    {
        if (sectionIndex && (int) i != *sectionIndex) continue;
        const auto& s = song.sections[i];
        if (s.options.empty()) continue;
        const double origin = sectionIndex ? 0.0 : s.startBar * bpb;
        for (const auto& c : s.current().chords)
            out.push_back ({ c.chord, origin + c.startBeat, c.beats });
    }
    return out;
}

midi::VoicedLanes voiceSong (const Song& song) { return midi::voiceProgression (timedChords (song)); }

midi::VoicedLanes voiceSection (const Song& song, int sectionIndex)
{
    return midi::voiceProgression (timedChords (song, sectionIndex));
}

std::vector<midi::MidiLane> toLanes (const midi::VoicedLanes& lanes)
{
    return { lanes.chords, lanes.bass, lanes.top };
}

std::vector<ArrangementChord> flattenArrangement (const Song& song, const theory::NamingOptions& naming,
                                                  std::optional<int> sectionIndex)
{
    std::vector<ArrangementChord> out;
    const auto chords = timedChords (song, sectionIndex);
    for (size_t i = 0; i < chords.size(); ++i)
    {
        const double end = i + 1 < chords.size() ? chords[i + 1].startBeat : chords[i].startBeat + chords[i].lengthBeats;
        out.push_back ({ theory::ChordNamer::name (chords[i].chord, naming), std::max (chords[i].lengthBeats, end - chords[i].startBeat) });
    }
    return out;
}

} // namespace so::song
