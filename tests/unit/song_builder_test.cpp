#include <catch2/catch_test_macros.hpp>

#include "core/song/MoodInterpreter.h"
#include "core/song/SongBuilder.h"
#include "core/song/SongMidi.h"
#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"

#include <cmath>
#include <set>

using namespace so;
using namespace so::song;
using theory::HarmonicFunction;

namespace
{
    analysis::AnalysisResult fMinorIdea()
    {
        analysis::AnalysisResult a;
        a.grid.bpm = 92.0;
        a.bars = 4;
        a.key = *theory::Key::parse ("F minor");
        a.keyConfidence = 0.9f;
        const char* sym[] = { "Fm9", "Dbmaj7", "Ab/C", "Ebsus4", "Eb" };
        const double start[] = { 0, 4, 8, 12, 14 };
        const double len[] = { 4, 4, 4, 2, 2 };
        for (int i = 0; i < 5; ++i)
        {
            analysis::ChordSegment s;
            s.id = i;
            s.startBeat = start[i] + 8.0;   // the idea starts at bar 3 of the capture
            s.beats = len[i];
            s.chord = *theory::ChordSymbolParser::tryParse (sym[i]);
            a.segments.push_back (s);
        }
        return a;
    }

    analysis::AnalysisResult cMajorIdea()
    {
        analysis::AnalysisResult a;
        a.grid.bpm = 100.0;
        a.key = *theory::Key::parse ("C major");
        const char* sym[] = { "C", "G/B", "Am7", "Fmaj7" };
        for (int i = 0; i < 4; ++i)
        {
            analysis::ChordSegment s;
            s.startBeat = i * 4.0;
            s.beats = 4.0;
            s.chord = *theory::ChordSymbolParser::tryParse (sym[i]);
            a.segments.push_back (s);
        }
        return a;
    }

    SongRequest request (std::string_view mood, analysis::AnalysisResult idea, std::uint64_t seed = 1,
                         IdeaRole role = IdeaRole::Verse, StructurePreset structure = StructurePreset::Auto)
    {
        SongRequest r;
        r.idea = std::move (idea);
        r.ideaRole = role;
        r.mood = MoodInterpreter::interpret (mood);
        r.seed = seed;
        r.structure = structure;
        return r;
    }

    std::vector<std::string> fingerprint (const Song& s)
    {
        std::vector<std::string> f;
        for (const auto& sec : s.sections)
            for (const auto& o : sec.options)
            {
                std::string line = sec.label + "|" + o.id + "|" + o.explanation + "|";
                for (const auto& c : o.chords)
                    line += theory::ChordNamer::name (c.chord) + "@" + std::to_string (c.startBeat) + "+" + std::to_string (c.beats) + " ";
                f.push_back (line);
            }
        f.push_back (s.arc);
        return f;
    }

    int sentences (const std::string& text)
    {
        int n = 0;
        for (size_t i = 0; i < text.size(); ++i)
            if (text[i] == '.' && (i + 1 == text.size() || text[i + 1] == ' '))
                ++n;
        return n;
    }

    void checkCoverage (const Song& song)
    {
        int bar = 0;
        for (const auto& sec : song.sections)
        {
            CHECK (sec.startBar == bar);
            bar += sec.bars;
            REQUIRE_FALSE (sec.options.empty());
            CHECK (sec.chosen >= 0);
            CHECK (sec.chosen < (int) sec.options.size());
            for (const auto& o : sec.options)
            {
                REQUIRE_FALSE (o.chords.empty());
                double t = 0.0;
                for (const auto& c : o.chords)
                {
                    CHECK (c.beats > 0.0);
                    CHECK (c.startBeat >= t - 1e-6);
                    t = c.startBeat + c.beats;
                }
                if (! sec.isIdea)
                {
                    CHECK (o.chords.front().startBeat == 0.0);
                    CHECK (std::fabs (t - sec.bars * 4.0) < 1e-6);
                }
                CHECK (t <= sec.bars * 4.0 + 1e-6);
            }
        }
        CHECK (bar == song.totalBars());
    }

    std::vector<SongRequest> varietyOfRequests()
    {
        std::vector<SongRequest> rs;
        rs.push_back (request ("dark late-night R&B, hopeful chorus", fMinorIdea(), 7));
        rs.push_back (request ("aggressive drill, minor, cold", {}, 3));
        rs.push_back (request ("uplifting gospel choir chords", cMajorIdea(), 11));
        rs.push_back (request ("lofi chill rainy day", {}, 5));
        rs.push_back (request ("happy summer pop", cMajorIdea(), 2, IdeaRole::Chorus));
        rs.push_back (request ("cinematic, epic, triumphant", fMinorIdea(), 9, IdeaRole::Hook));
        rs.push_back (request ("smooth neo-soul with lush extensions", cMajorIdea(), 4, IdeaRole::Loop));
        rs.push_back (request ("hard trap beat, 808s", fMinorIdea(), 13, IdeaRole::Loop));
        rs.push_back (request ("euphoric EDM", {}, 21));
        rs.push_back (request ("jazzy tritone subs", cMajorIdea(), 8));
        rs.push_back (request ("country", {}, 17));
        rs.push_back (request ("afrobeats, warm and bouncy", fMinorIdea(), 6));
        return rs;
    }
}

TEST_CASE ("Every generated chord survives naming and parsing", "[song][builder]")
{
    for (const auto& req : varietyOfRequests())
    {
        const auto song = SongBuilder::build (req);
        for (const auto& sec : song.sections)
            for (const auto& o : sec.options)
                for (const auto& c : o.chords)
                {
                    const auto name = theory::ChordNamer::name (c.chord);
                    const auto back = theory::ChordSymbolParser::tryParse (name);
                    INFO (sec.label << " " << o.id << " " << name);
                    REQUIRE (back.has_value());
                    CHECK (back->sameHarmony (c.chord));
                    CHECK_FALSE (c.roman.empty());
                }
    }
}

TEST_CASE ("The idea section keeps the heard chords exactly", "[song][builder]")
{
    const auto idea = fMinorIdea();
    for (auto role : { IdeaRole::Verse, IdeaRole::Chorus, IdeaRole::Hook, IdeaRole::Loop })
        for (std::uint64_t seed : { 1ull, 2ull, 99ull })
        {
            const auto song = SongBuilder::build (request ("dark late-night R&B, hopeful chorus", idea, seed, role));
            int ideaSections = 0;
            for (const auto& sec : song.sections)
            {
                if (! sec.isIdea) continue;
                ++ideaSections;
                REQUIRE (sec.options.size() == 1);
                const auto& chords = sec.current().chords;
                // Tiled: every 16 beats the idea starts over, chords identical to the analysis.
                REQUIRE (chords.size() == idea.segments.size() * (size_t) (sec.bars / 4));
                for (size_t i = 0; i < chords.size(); ++i)
                {
                    const auto& seg = idea.segments[i % idea.segments.size()];
                    CHECK (chords[i].chord == seg.chord);
                    CHECK (chords[i].beats == seg.beats);
                    CHECK (chords[i].startBeat == (seg.startBeat - 8.0) + 16.0 * (double) (i / idea.segments.size()));
                }
                CHECK (sec.key == idea.key);
            }
            CHECK (ideaSections >= 1);
        }
}

TEST_CASE ("Chorus energy is at least the verse energy by default", "[song][builder]")
{
    for (const auto& req : varietyOfRequests())
    {
        const auto song = SongBuilder::build (req);
        float verse = -1.0f, chorusMin = 2.0f;
        for (const auto& sec : song.sections)
        {
            if (sec.section == Section::Verse) verse = std::max (verse, sec.current().energy);
            if (sec.section == Section::Chorus) chorusMin = std::min (chorusMin, sec.current().energy);
        }
        if (verse >= 0.0f && chorusMin <= 1.0f)
            CHECK (chorusMin >= verse);
    }
}

TEST_CASE ("A dark mood produces mostly minor-family harmony", "[song][builder]")
{
    for (std::uint64_t seed : { 1ull, 2ull, 3ull, 4ull })
    {
        const auto song = SongBuilder::build (request ("very dark aggressive drill, minor, cold", {}, seed));
        REQUIRE (song.key.mode == theory::Mode::Minor);
        double minorBeats = 0.0, total = 0.0;
        for (const auto& sec : song.sections)
            for (const auto& c : sec.current().chords)
            {
                const int rel = theory::wrapPc (c.chord.root.pc() - sec.key.tonic.pc());
                const auto fam = theory::info (c.chord.quality).family;
                const bool minorQuality = fam == theory::QualityFamily::Minor || fam == theory::QualityFamily::Diminished;
                // Major chords that live in natural minor or Phrygian (bII, bIII, bVI, bVII) count as minor-key harmony.
                const bool minorKeyMajor = ! minorQuality && (rel == 1 || rel == 3 || rel == 8 || rel == 10);
                const bool suspendedHome = fam == theory::QualityFamily::Suspended && rel == 0;
                if (minorQuality || minorKeyMajor || suspendedHome) minorBeats += c.beats;
                total += c.beats;
            }
        INFO ("seed " << seed << " minor share " << minorBeats / total);
        CHECK (minorBeats / total >= 0.75);
    }

    const auto bright = SongBuilder::build (request ("happy uplifting summer pop", {}, 1));
    CHECK (bright.key.mode == theory::Mode::Major);
}

TEST_CASE ("Each section ends on a chord that leads into the next", "[song][builder]")
{
    for (const auto& req : varietyOfRequests())
    {
        const auto song = SongBuilder::build (req);
        for (size_t i = 0; i + 1 < song.sections.size(); ++i)
        {
            const auto& sec = song.sections[i];
            const auto& target = song.sections[i + 1].current().chords.front().chord;
            for (const auto& o : sec.options)
            {
                const auto& last = o.chords.back();
                const auto t = transitionBetween (last.chord, target);
                const bool functional = last.function == HarmonicFunction::Dominant || last.function == HarmonicFunction::Predominant;
                INFO (sec.label << " option " << o.id << ": " << theory::ChordNamer::name (last.chord) << " -> "
                      << theory::ChordNamer::name (target) << " (" << toString (t) << ")");
                CHECK (t != Transition::None);
                if (! sec.isIdea)
                    CHECK ((isStrongTransition (t) || (t == Transition::CommonTone && functional)));
            }
        }
        // The song ends at home.
        const auto& outro = song.sections.back();
        if (outro.section == Section::Outro)
            for (const auto& o : outro.options)
                CHECK (o.chords.back().chord.root.pc() == outro.key.tonic.pc());
    }
}

TEST_CASE ("Builds are deterministic per seed and Shuffle changes them", "[song][builder]")
{
    const auto a = SongBuilder::build (request ("dark late-night R&B, hopeful chorus", fMinorIdea(), 42));
    const auto b = SongBuilder::build (request ("dark late-night R&B, hopeful chorus", fMinorIdea(), 42));
    CHECK (fingerprint (a) == fingerprint (b));

    int different = 0;
    for (std::uint64_t seed = 43; seed < 48; ++seed)
        if (fingerprint (SongBuilder::build (request ("dark late-night R&B, hopeful chorus", fMinorIdea(), seed))) != fingerprint (a))
            ++different;
    CHECK (different >= 3);
}

TEST_CASE ("Options within a section are genuinely different", "[song][builder]")
{
    for (const auto& req : varietyOfRequests())
    {
        const auto song = SongBuilder::build (req);
        for (const auto& sec : song.sections)
        {
            if (sec.isIdea || sec.repeatOf >= 0) continue;
            if (sec.section == Section::Verse || sec.section == Section::Chorus || sec.section == Section::PreChorus
                || sec.section == Section::Bridge)
                CHECK (sec.options.size() == 3);
            std::set<std::string> seqs, ids;
            for (const auto& o : sec.options)
            {
                std::string s;
                for (const auto& c : o.body) s += theory::ChordNamer::name (c.chord) + " ";
                seqs.insert (s);
                ids.insert (o.id);
                INFO (o.explanation);
                CHECK (sentences (o.explanation) <= 3);
                CHECK (o.explanation.find ('(') != std::string::npos);
                CHECK (o.explanation.find ("\xE2\x80\x94") == std::string::npos);
                CHECK (o.energy >= 0.0f);
                CHECK (o.energy <= 1.0f);
                CHECK (o.tension >= 0.0f);
                CHECK (o.tension <= 1.0f);
                CHECK_FALSE (o.title.empty());
            }
            CHECK (seqs.size() == sec.options.size());
            CHECK (ids.size() == sec.options.size());
        }
        CHECK_FALSE (song.arc.empty());
        CHECK (song.arc.find ("\xE2\x80\x94") == std::string::npos);
    }
}

TEST_CASE ("Empty analysis still builds a full song", "[song][builder]")
{
    for (const char* text : { "", "dark late-night R&B, hopeful chorus", "happy pop", "cinematic" })
    {
        SongRequest r;
        r.mood = MoodInterpreter::interpret (text);
        const auto song = SongBuilder::build (r);
        REQUIRE (song.sections.size() >= 6);
        checkCoverage (song);
        for (const auto& sec : song.sections)
        {
            CHECK_FALSE (sec.isIdea);
            if (sec.repeatOf < 0) CHECK (sec.options.size() >= 2);
        }
        CHECK (song.style != Style::Auto);
    }
}

TEST_CASE ("All structures and idea roles build", "[song][builder]")
{
    std::vector<SectionSpec> custom { { Section::Intro, 2, "" }, { Section::Chorus, 4, "" }, { Section::Verse, 8, "" },
                                      { Section::Chorus, 4, "" }, { Section::Bridge, 4, "" }, { Section::Chorus, 4, "" },
                                      { Section::PostChorus, 4, "" }, { Section::Breakdown, 4, "" }, { Section::Outro, 2, "" } };
    for (int p = 0; p < (int) StructurePreset::Count; ++p)
        for (int role = 0; role < (int) IdeaRole::Count; ++role)
            for (int withIdea = 0; withIdea < 2; ++withIdea)
            {
                auto req = request ("soulful, hopeful chorus", withIdea ? fMinorIdea() : analysis::AnalysisResult {},
                                    (std::uint64_t) (p * 10 + role), (IdeaRole) role, (StructurePreset) p);
                if ((StructurePreset) p == StructurePreset::Custom) req.customStructure = custom;
                const auto song = SongBuilder::build (req);
                INFO ("preset " << toString ((StructurePreset) p) << " role " << toString ((IdeaRole) role) << " idea " << withIdea);
                REQUIRE_FALSE (song.sections.empty());
                checkCoverage (song);
                if (withIdea)
                    CHECK (std::any_of (song.sections.begin(), song.sections.end(), [] (const SongSection& s) { return s.isIdea; }));
            }

    const auto trap = SongBuilder::build (request ("trap", fMinorIdea(), 1, IdeaRole::Hook, StructurePreset::HipHopTrap));
    CHECK (trap.sections[1].label == "Hook 1");
    CHECK (trap.sections[1].isIdea);
    const auto edm = SongBuilder::build (request ("edm", {}, 1, IdeaRole::Verse, StructurePreset::EDM));
    CHECK (edm.sections[2].label == "Drop 1");
}

TEST_CASE ("Repeated sections reuse the chosen option and choose() propagates", "[song][builder]")
{
    auto song = SongBuilder::build (request ("dark late-night R&B, hopeful chorus", fMinorIdea(), 7));
    std::vector<int> choruses;
    for (int i = 0; i < (int) song.sections.size(); ++i)
        if (song.sections[(size_t) i].section == Section::Chorus) choruses.push_back (i);
    REQUIRE (choruses.size() >= 2);
    const auto& c1 = song.sections[(size_t) choruses[0]];
    const auto& c2 = song.sections[(size_t) choruses[1]];
    CHECK (c2.repeatOf == choruses[0]);
    CHECK (c2.chosen == c1.chosen);
    CHECK (c2.current().id == c1.current().id);
    CHECK (c2.current().body.size() == c1.current().body.size());

    SongBuilder::choose (song, choruses[1], 2);
    for (int idx : choruses)
        if (song.sections[(size_t) idx].key == song.key)
            CHECK (song.sections[(size_t) idx].current().id == song.sections[(size_t) choruses[0]].options[2].id);
}

TEST_CASE ("A key change option moves the final chorus and is set up by a dominant", "[song][builder]")
{
    bool found = false;
    for (std::uint64_t seed = 1; seed < 40 && ! found; ++seed)
    {
        auto song = SongBuilder::build (request ("uplifting pop", cMajorIdea(), seed));
        for (int i = 0; i < (int) song.sections.size() && ! found; ++i)
        {
            const auto& sec = song.sections[(size_t) i];
            for (int o = 0; o < (int) sec.options.size(); ++o)
                if (sec.options[(size_t) o].keyShiftAfter != 0)
                {
                    SongBuilder::choose (song, i, o);
                    const auto& bridge = song.sections[(size_t) i];
                    const auto& next = song.sections[(size_t) i + 1];
                    CHECK (next.key.tonic.pc() == theory::wrapPc (song.key.tonic.pc() + bridge.current().keyShiftAfter));
                    CHECK (transitionBetween (bridge.current().chords.back().chord, next.current().chords.front().chord) == Transition::Dominant);
                    CHECK (next.variation);
                    found = true;
                    break;
                }
        }
    }
    CHECK (found);
}

TEST_CASE ("Loop ideas drive the other sections", "[song][builder]")
{
    const auto song = SongBuilder::build (request ("smooth R&B", fMinorIdea(), 3, IdeaRole::Loop));
    for (const auto& sec : song.sections)
        if (sec.section == Section::Chorus && sec.repeatOf < 0)
        {
            int derived = 0;
            for (const auto& o : sec.options)
                if (o.id.rfind ("loop-", 0) == 0) ++derived;
            CHECK (derived >= 2);
        }
}

TEST_CASE ("Song to MIDI lanes and drag-strip symbols", "[song][midi]")
{
    const auto song = SongBuilder::build (request ("dark late-night R&B, hopeful chorus", fMinorIdea(), 7));
    const auto lanes = voiceSong (song);
    CHECK_FALSE (lanes.chords.notes.empty());
    CHECK_FALSE (lanes.bass.notes.empty());
    CHECK_FALSE (lanes.top.notes.empty());
    double lastEnd = 0.0;
    for (const auto& n : lanes.bass.notes) lastEnd = std::max (lastEnd, n.startBeat + n.lengthBeats);
    CHECK (lastEnd <= song.totalBars() * 4.0 + 1e-6);
    CHECK (toLanes (lanes).size() == 3);

    const auto section = voiceSection (song, 1);
    double first = 1e9;
    for (const auto& n : section.chords.notes) first = std::min (first, n.startBeat);
    CHECK (first == 0.0);

    const auto flat = flattenArrangement (song);
    double beats = 0.0;
    for (const auto& c : flat)
    {
        beats += c.beats;
        CHECK (theory::ChordSymbolParser::tryParse (c.symbol).has_value());
    }
    CHECK (std::fabs (beats - song.totalBars() * 4.0) < 1e-6);
    CHECK (flat.front().symbol == theory::ChordNamer::name (song.sections.front().current().chords.front().chord));
}

TEST_CASE ("Tempo comes from the host, the idea, or the style", "[song][builder]")
{
    CHECK (SongBuilder::build (request ("drill", {}, 1)).bpm == 142.0);
    CHECK (SongBuilder::build (request ("drill", fMinorIdea(), 1)).bpm == 92.0);
    auto r = request ("drill", fMinorIdea(), 1);
    r.bpm = 140.0;
    CHECK (SongBuilder::build (r).bpm == 140.0);
}
