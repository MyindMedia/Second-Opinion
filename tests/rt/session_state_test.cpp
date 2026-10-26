// Save and recall (FR-ST-01), settings (FR-SET), undo snapshots (FR-ST-02) and the audio cache,
// through the real processor and SessionEngine. See docs/adr/0011-session-persistence.md.

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>

#include "bridge/Json.h"
#include "bridge/NativeDragStrip.h"
#include "core/theory/ChordSymbolParser.h"
#include "plugin/PluginProcessor.h"
#include "services/AudioCache.h"
#include "services/SessionEngine.h"
#include "services/SongBridge.h"

namespace json = so::bridge::json;
using juce::var;

namespace
{
    so::theory::Chord chord (const char* s)
    {
        auto c = so::theory::ChordSymbolParser::tryParse (s);
        REQUIRE (c.has_value());
        return *c;
    }

    /** 4 bars of F minor: Fm9 | Dbmaj7 | Ab/C | Eb7, a melody on the 16th grid. */
    so::analysis::AnalysisResult smallAnalysis (bool offGridMelody = false)
    {
        so::analysis::AnalysisResult a;
        a.grid.bpm = 92.0;
        a.grid.beatsPerBar = 4;
        a.grid.downbeatSec = 0.0123456789;
        a.bars = 4;
        a.key = *so::theory::Key::parse ("F minor");
        a.keyConfidence = 0.83f;

        const char* symbols[] { "Fm9", "Dbmaj7", "Ab/C", "Eb7" };
        for (int i = 0; i < 4; ++i)
        {
            so::analysis::ChordSegment s;
            s.id = i;
            s.startBeat = i * 4.0;
            s.beats = 4.0;
            s.chord = chord (symbols[i]);
            s.confidence = 0.5f + 0.1f * (float) i;
            s.alternatives.push_back ({ chord (i == 2 ? "Cm" : "Fm7"), 0.12f });
            s.alternatives.push_back ({ chord ("Bbm7/F"), 0.031f });
            a.segments.push_back (s);
        }

        const int pitches[] { 72, 75, 77, 75, 72, 70, 68, 70, 72, 75, 79, 77 };
        double beat = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double len = offGridMelody && i == 5 ? 1.0 / 3.0 : (i % 3 == 0 ? 1.5 : 1.25);
            a.melody.push_back ({ pitches[i], beat, len });
            beat += len;
        }
        return a;
    }

    var request (std::uint64_t seed)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("schemaVersion", 1);
        o->setProperty ("description", juce::String::fromUTF8 ("dark late-night R&B, hopeful chorus \xe2\x80\x94 sunrise bridge"));
        o->setProperty ("moods", juce::Array<var> { "dark", "hopeful" });
        o->setProperty ("ideaSection", "verse");
        o->setProperty ("structure", "auto");
        o->setProperty ("seed", (juce::int64) seed);
        o->setProperty ("useOnlineAi", false);
        return var (o);
    }

    juce::String str (const var& v) { return juce::JSON::toString (v, true); }

    juce::String analysisJson (SessionEngine& e)
    {
        REQUIRE (e.analysis().has_value());
        return str (json::analysis (*e.analysis(), "captured", "Lead vocal"));
    }

    juce::String suggestionsJson (SessionEngine& e) { return str (json::suggestions (e.suggestions(), e.currentSeed())); }

    juce::String songJson (SessionEngine& e)
    {
        REQUIRE (e.currentSong() != nullptr);
        return str (SongBridge::toJson (*e.currentSong(), e.currentSongMeta()));
    }

    juce::MemoryBlock stateOf (SecondOpinionProcessor& p)
    {
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        return mb;
    }

    var settings (const char* jsonText) { return juce::JSON::parse (jsonText); }

    int ppqOf (const std::vector<std::uint8_t>& smf) { return smf.size() > 13 ? (smf[12] << 8) | smf[13] : -1; }

    std::vector<int> noteOnVelocities (const std::vector<std::uint8_t>& smf)
    {
        // Every note-on status byte (0x9n) is followed by key and velocity in our writer's output.
        std::vector<int> v;
        for (size_t i = 14; i + 2 < smf.size(); ++i)
            if ((smf[i] & 0xf0) == 0x90 && smf[i + 1] < 128 && smf[i + 2] > 0 && smf[i + 2] < 128)
                v.push_back (smf[i + 2]);
        return v;
    }
}

TEST_CASE ("Session state round-trip restores analysis, suggestions and song exactly", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String analysisA, suggestionsA, songA, hashA;
    std::uint64_t seedA = 0;
    int genA = 0;
    juce::MemoryBlock blob;

    {
        SecondOpinionProcessor p;
        if (auto* t = p.apvts.getParameter (so::params::tension))
            t->setValueNotifyingHost (0.8f);

        auto& e = p.getEngine();
        e.adoptAnalysis (smallAnalysis(), "captured", "Lead vocal");
        e.lock (juce::Array<var> { "2" }, true);
        REQUIRE (e.generateNow (3));
        REQUIRE (e.buildSongNow (request (2)));
        REQUIRE (e.currentSong()->sections.size() > 2);
        REQUIRE ((bool) e.chooseSectionOption (2, 1)["ok"]);

        // Corrections after generating and building: the saved recipes must still use their own input.
        REQUIRE ((bool) e.setChord ("1", "Db6")["ok"]);

        REQUIRE ((bool) e.setSettings (settings (R"({"quality":"accurate","sourceHint":"keys","ppq":480,"velocity":"flat","previewSound":"pad"})"))["ok"]);
        p.session.setProperty ("ui_selected", juce::String (e.suggestions()[1].id), nullptr);
        p.session.setProperty ("ui_favorites", R"(["s1","s4"])", nullptr);

        analysisA = analysisJson (e);
        suggestionsA = suggestionsJson (e);
        songA = songJson (e);
        seedA = e.currentSeed();
        genA = e.currentGeneration();
        blob = stateOf (p);
    }

    // Captured audio is not stored: the blob stays small.
    CHECK (blob.getSize() < 64 * 1024);

    SecondOpinionProcessor q;
    // Different live parameters must not change the restored suggestions (they replay their recipe).
    if (auto* t = q.apvts.getParameter (so::params::smoothness))
        t->setValueNotifyingHost (0.1f);
    q.setStateInformation (blob.getData(), (int) blob.getSize());

    auto& e = q.getEngine();
    CHECK (analysisJson (e) == analysisA);
    CHECK (suggestionsJson (e) == suggestionsA);
    CHECK (songJson (e) == songA);
    CHECK (e.currentSeed() == seedA);
    CHECK (e.currentGeneration() == genA);
    CHECK (e.seedHistory() == std::vector<std::uint64_t> { 0, 3 });

    CHECK (e.userSettings().ppq == 480);
    CHECK_FALSE (e.userSettings().humanize);
    CHECK (e.analyzerOptions().quality == so::analysis::AnalysisQuality::Accurate);
    CHECK (e.analyzerOptions().source == so::analysis::SourceHint::Keys);
    CHECK (q.previewSound() == SecondOpinionProcessor::PreviewSound::Pad);
    CHECK (q.session.getProperty ("ui_favorites").toString() == R"(["s1","s4"])");
    CHECK ((int) q.session.getProperty ("stateVersion") == 2);

    // Saving the restored session gives the same engine state again (stable).
    const auto again = stateOf (q);
    SecondOpinionProcessor r;
    r.setStateInformation (again.getData(), (int) again.getSize());
    CHECK (suggestionsJson (r.getEngine()) == suggestionsA);
    CHECK (songJson (r.getEngine()) == songA);
}

TEST_CASE ("Analysis tree codec is exact, on and off the melody grid", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (bool offGrid : { false, true })
    {
        const auto a = smallAnalysis (offGrid);
        const auto tree = SessionEngine::analysisToTree (a);
        CHECK (tree.getProperty ("melody").toString().startsWith (offGrid ? "d:" : "t1:"));
        const auto back = SessionEngine::analysisFromTree (juce::ValueTree::fromXml (*tree.createXml()));
        REQUIRE (back.has_value());
        CHECK (str (json::analysis (*back, "file", "x")) == str (json::analysis (a, "file", "x")));
        REQUIRE (back->melody.size() == a.melody.size());
        for (size_t i = 0; i < a.melody.size(); ++i)
            CHECK ((juce::exactlyEqual (back->melody[i].startBeat, a.melody[i].startBeat) && juce::exactlyEqual (back->melody[i].beats, a.melody[i].beats)));
    }
}

TEST_CASE ("A version 1 blob migrates forward and its settings are honoured", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::XmlElement root ("SecondOpinionState");
    root.setAttribute ("stateVersion", 1);
    auto* session = root.createNewChildElement ("Session");
    session->setAttribute ("stateVersion", 1);
    session->setAttribute ("previewSound", 1);
    session->setAttribute ("uiWidth", 1200);
    session->setAttribute ("ui_selected", "s3");
    session->setAttribute ("ui_settings", R"({"notation":"jazz","quality":"fast","sourceHint":"vocal","ppq":480,"velocity":"flat","previewSound":"off"})");

    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary (root, blob);

    SecondOpinionProcessor p;
    p.setStateInformation (blob.getData(), (int) blob.getSize());

    CHECK ((int) p.session.getProperty ("stateVersion") == 2);
    CHECK ((int) p.session.getProperty ("uiWidth") == 1200);
    CHECK_FALSE (p.getEngine().hasAnalysis());
    CHECK (p.getEngine().analyzerOptions().quality == so::analysis::AnalysisQuality::Fast);
    CHECK (p.getEngine().analyzerOptions().source == so::analysis::SourceHint::Vocal);
    CHECK (p.getEngine().userSettings().ppq == 480);
    CHECK (p.previewSound() == SecondOpinionProcessor::PreviewSound::Off);   // ui_settings wins over v1's int

    const auto saved = stateOf (p);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("stateVersion") == 2);

    // A v1 blob without settings falls back to defaults and keeps v1's preview sound.
    session->removeAttribute ("ui_settings");
    juce::AudioProcessor::copyXmlToBinary (root, blob);
    SecondOpinionProcessor q;
    q.setStateInformation (blob.getData(), (int) blob.getSize());
    CHECK (q.getEngine().analyzerOptions().quality == so::analysis::AnalysisQuality::Balanced);
    CHECK (q.previewSound() == SecondOpinionProcessor::PreviewSound::Pad);
}

TEST_CASE ("500 garbage and mutated state blobs never break the plugin", "[state][fuzz]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String goodXml;
    juce::MemoryBlock good;
    {
        SecondOpinionProcessor p;
        p.getEngine().adoptAnalysis (smallAnalysis(), "file", "Idea.wav");
        p.getEngine().buildSongNow (request (1));
        good = stateOf (p);
        goodXml = juce::AudioProcessor::getXmlFromBinary (good.getData(), (int) good.getSize())->toString();
    }

    SecondOpinionProcessor p;
    juce::Random rng (20261026);
    int parsed = 0;

    for (int i = 0; i < 500; ++i)
    {
        juce::MemoryBlock blob;
        if (i % 2 == 0)
        {
            // Pure noise, sometimes behind a valid binary-XML header.
            blob.setSize ((size_t) rng.nextInt (2048), true);
            for (size_t b = 0; b < blob.getSize(); ++b)
                blob[b] = (char) rng.nextInt (256);
            if (i % 4 == 0 && blob.getSize() >= 8)
            {
                const juce::uint32 magic = 0x21324356, size = (juce::uint32) blob.getSize() - 8;
                std::memcpy (blob.getData(), &magic, 4);
                std::memcpy (static_cast<char*> (blob.getData()) + 4, &size, 4);
            }
        }
        else
        {
            // The real document with a few characters changed: valid XML with broken values.
            auto text = goodXml;
            for (int k = 0, n = 1 + rng.nextInt (6); k < n; ++k)
            {
                const int at = rng.nextInt (text.length());
                const char pool[] = "0123456789-.,;:e\"<>/ aZ";
                text = text.substring (0, at) + juce::String::charToString ((juce::juce_wchar) pool[rng.nextInt ((int) sizeof (pool) - 1)])
                     + text.substring (at + 1);
            }
            if (auto xml = juce::XmlDocument::parse (text))
            {
                juce::AudioProcessor::copyXmlToBinary (*xml, blob);
                ++parsed;
            }
            else
                blob.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
        }

        p.setStateInformation (blob.getData(), (int) blob.getSize());
        const auto out = stateOf (p);
        REQUIRE (out.getSize() > 0);
        if (p.getEngine().hasAnalysis())
            REQUIRE_FALSE (p.getEngine().suggestions().empty());
    }
    CHECK (parsed > 50);

    // Still healthy afterwards.
    p.setStateInformation (good.getData(), (int) good.getSize());
    CHECK (p.getEngine().hasAnalysis());
    CHECK (p.getEngine().currentSong() != nullptr);
}

TEST_CASE ("Settings change analyzer options and MIDI export bytes", "[settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SecondOpinionProcessor p;
    auto& e = p.getEngine();

    p.progression.id = "s2";
    p.progression.title = "Borrowed lift";
    p.progression.bpm = 92.0;
    p.progression.chords = { { chord ("Fm9"), 0.0, 4.0 }, { chord ("Dbmaj7"), 4.0, 4.0 }, { chord ("Ab/C"), 8.0, 4.0 } };

    CHECK (e.analyzerOptions().quality == so::analysis::AnalysisQuality::Balanced);
    const auto humanized960 = NativeDragStrip::laneBytes (p, NativeDragStrip::Lane::All);
    CHECK (ppqOf (humanized960) == 960);
    const auto v1 = noteOnVelocities (humanized960);
    REQUIRE (v1.size() > 6);
    CHECK (std::set<int> (v1.begin(), v1.end()).size() > 2);   // varied
    CHECK (NativeDragStrip::laneBytes (p, NativeDragStrip::Lane::All) == humanized960);   // seeded: deterministic

    REQUIRE ((bool) e.setSettings (settings (R"({"quality":"fast","sourceHint":"loop","ppq":480,"velocity":"flat"})"))["ok"]);
    CHECK (e.analyzerOptions().quality == so::analysis::AnalysisQuality::Fast);
    CHECK (e.analyzerOptions().source == so::analysis::SourceHint::Loop);

    const auto flat480 = NativeDragStrip::laneBytes (p, NativeDragStrip::Lane::All);
    CHECK (ppqOf (flat480) == 480);
    for (int v : noteOnVelocities (flat480))
        CHECK (v == 100);
    CHECK (flat480 != humanized960);

    // A JSON string works too, and an invalid PPQ falls back to 960.
    REQUIRE ((bool) e.setSettings (R"({"ppq":123})")["ok"]);
    CHECK (ppqOf (NativeDragStrip::laneBytes (p, NativeDragStrip::Lane::Chords)) == 960);
    CHECK_FALSE ((bool) e.setSettings (var (42))["ok"]);
    CHECK (p.session.getProperty ("ui_settings").toString().contains ("123"));
}

TEST_CASE ("restoreSnapshot follows UI undo: corrections, locks, generations and songs", "[undo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SecondOpinionProcessor p;
    auto& e = p.getEngine();
    e.adoptAnalysis (smallAnalysis(), "captured", "Lead vocal");

    const auto analysis0 = analysisJson (e);
    const auto suggestions0 = suggestionsJson (e);
    const int gen0 = e.currentGeneration();

    // Correct a chord, then undo it.
    REQUIRE ((bool) e.setChord ("0", "C")["ok"]);
    CHECK (analysisJson (e) != analysis0);
    auto seg = juce::JSON::parse (R"({"segments":[{"id":"0","symbol":"Fm9","confidence":0.5,"userLocked":false,"userCorrected":false}]})");
    CHECK ((bool) e.restoreSnapshot (seg)["ok"]);
    CHECK (analysisJson (e) == analysis0);

    // Generate twice, undo back to the first set, redo forward.
    REQUIRE (e.generateNow (7));
    const int gen1 = e.currentGeneration();
    const auto suggestions1 = suggestionsJson (e);
    REQUIRE (e.generateNow (8));
    CHECK ((bool) e.restoreSnapshot (juce::JSON::parse ("{\"generation\":" + juce::String (gen0) + "}"))["ok"]);
    CHECK (suggestionsJson (e) == suggestions0);
    CHECK ((bool) e.restoreSnapshot (juce::JSON::parse ("{\"generation\":" + juce::String (gen1) + "}"))["ok"]);
    CHECK (suggestionsJson (e) == suggestions1);
    CHECK_FALSE ((bool) e.restoreSnapshot (juce::JSON::parse ("{\"generation\":99999}"))["ok"]);

    // 60 generations: the oldest of the last 50 is still reachable.
    for (int i = 0; i < 60; ++i)
        REQUIRE (e.generateNow (100 + (std::uint64_t) i));
    CHECK ((bool) e.restoreSnapshot (juce::JSON::parse ("{\"generation\":" + juce::String (e.currentGeneration() - 50) + "}"))["ok"]);
    CHECK (e.currentSeed() == 109);

    // Songs: build, pick an option, shuffle, undo to the first (with its pick), then to none.
    REQUIRE (e.buildSongNow (request (0)));
    REQUIRE ((bool) e.chooseSectionOption (2, 1)["ok"]);
    const int songA = e.currentSongGeneration();
    const auto songJsonA = songJson (e);
    REQUIRE (e.buildSongNow (request (1)));
    CHECK (e.currentSongGeneration() != songA);
    CHECK (songJson (e) != songJsonA);

    juce::StringArray chosen;
    const auto parsedA = juce::JSON::parse (songJsonA);
    if (const auto* secs = parsedA["sections"].getArray())
        for (const auto& sec : *secs)
            chosen.add (sec["chosen"].toString());
    CHECK (chosen.contains ("1"));
    const auto snapA = juce::JSON::parse ("{\"song\":{\"generation\":" + juce::String (songA) + ",\"chosen\":[" + chosen.joinIntoString (",") + "]}}");
    CHECK ((bool) e.restoreSnapshot (snapA)["ok"]);
    CHECK (e.currentSongGeneration() == songA);
    CHECK (songJson (e) == songJsonA);

    CHECK ((bool) e.restoreSnapshot (juce::JSON::parse (R"({"song":null})"))["ok"]);
    CHECK (e.currentSong() == nullptr);
}

TEST_CASE ("Audio cache: SHA-256, FLAC write and clear", "[cache]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CHECK (so::cache::Sha256::of ("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (so::cache::Sha256::of ("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string million (1000000, 'a');
    CHECK (so::cache::Sha256::of (million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    so::analysis::AudioClip clip;
    clip.sampleRate = 22050.0;
    for (int i = 0; i < 22050; ++i)
        clip.samples.push_back (0.5f * (float) std::sin (i * 0.05));

    const auto hash = so::cache::hashClip (clip);
    CHECK (hash.length() == 64);
    clip.samples[100] += 0.001f;
    CHECK (so::cache::hashClip (clip) != hash);
    clip.samples[100] -= 0.001f;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("so_cache_test");
    dir.deleteRecursively();
    REQUIRE (so::cache::writeClip (clip, hash, dir));
    const auto file = dir.getChildFile (hash + ".flac");
    REQUIRE (file.existsAsFile());

    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (file.createInputStream().release(), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->lengthInSamples == 22050);
    CHECK (juce::roundToInt (reader->sampleRate) == 22050);

    CHECK (so::cache::clear (dir) == 1);
    CHECK_FALSE (file.existsAsFile());
    dir.deleteRecursively();
}
