// so_song_demo: prints a full song built from an F minor idea and a mood description.
// Usage: so_song_demo [seed] [description...]

#include "core/song/MoodInterpreter.h"
#include "core/song/SongBuilder.h"
#include "core/song/SongMidi.h"
#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace so;

int main (int argc, char** argv)
{
    std::uint64_t seed = argc > 1 ? std::strtoull (argv[1], nullptr, 10) : 7;
    std::string description = "dark late-night R&B, hopeful chorus";
    if (argc > 2)
    {
        description.clear();
        for (int i = 2; i < argc; ++i)
            description += (i > 2 ? " " : "") + std::string (argv[i]);
    }

    // The heard idea: Fm9 | Dbmaj7 | Ab/C | Ebsus4 Eb at 92 BPM.
    analysis::AnalysisResult idea;
    idea.grid.bpm = 92.0;
    idea.bars = 4;
    idea.key = *theory::Key::parse ("F minor");
    idea.keyConfidence = 0.9f;
    const char* symbols[] = { "Fm9", "Dbmaj7", "Ab/C", "Ebsus4", "Eb" };
    const double starts[] = { 0, 4, 8, 12, 14 };
    const double lengths[] = { 4, 4, 4, 2, 2 };
    for (int i = 0; i < 5; ++i)
    {
        analysis::ChordSegment seg;
        seg.id = i;
        seg.startBeat = starts[i];
        seg.beats = lengths[i];
        seg.chord = *theory::ChordSymbolParser::tryParse (symbols[i]);
        seg.confidence = 0.9f;
        idea.segments.push_back (seg);
    }

    const auto reading = song::MoodInterpreter::read (description);

    song::SongRequest req;
    req.idea = idea;
    req.ideaRole = song::IdeaRole::Verse;
    req.mood = reading.profile;
    req.seed = seed;

    const auto s = song::SongBuilder::build (req);

    std::printf ("Description: \"%s\"\n", description.c_str());
    std::printf ("Heard:");
    for (const auto& t : reading.terms)
        std::printf (" %s%s%s", t.negated ? "not " : "", t.phrase.c_str(),
                     t.section ? (" [" + std::string (song::toString (*t.section)) + "]").c_str() : "");
    std::printf ("\nMood JSON:\n%s\n", song::toJson (reading.profile).c_str());

    std::printf ("%s, %.0f BPM, style %s, %d bars, seed %llu\n\n", s.key.toString().c_str(), s.bpm,
                 std::string (song::toString (s.style)).c_str(), s.totalBars(), (unsigned long long) seed);

    for (const auto& sec : s.sections)
    {
        std::printf ("== %s  (bar %d, %d bars, %s)%s\n", sec.label.c_str(), sec.startBar + 1, sec.bars,
                     sec.key.toString().c_str(), sec.isIdea ? "  [your idea]" : "");
        if (sec.repeatOf >= 0)
        {
            std::printf ("   %s\n", sec.variationNote.c_str());
            const auto& o = sec.current();
            std::printf ("   ");
            for (const auto& c : o.chords)
                std::printf ("%s(%g) ", theory::ChordNamer::name (c.chord).c_str(), c.beats);
            std::printf ("\n\n");
            continue;
        }
        for (size_t i = 0; i < sec.options.size(); ++i)
        {
            const auto& o = sec.options[i];
            std::printf ("  %c %zu. %s  [energy %.2f, tension %.2f]\n     ", (int) i == sec.chosen ? '*' : ' ', i + 1,
                         o.title.c_str(), (double) o.energy, (double) o.tension);
            for (const auto& c : o.chords)
                std::printf ("%s(%g) ", theory::ChordNamer::name (c.chord).c_str(), c.beats);
            std::printf ("\n     ");
            for (const auto& c : o.chords)
                std::printf ("%s ", c.roman.c_str());
            std::printf ("\n     %s\n", o.explanation.c_str());
        }
        std::printf ("\n");
    }
    std::printf ("Arc: %s\n", s.arc.c_str());

    const auto lanes = song::voiceSong (s);
    std::printf ("\nMIDI lanes: chords %zu notes, bass %zu notes, top %zu notes\n",
                 lanes.chords.notes.size(), lanes.bass.notes.size(), lanes.top.notes.size());
    return 0;
}
