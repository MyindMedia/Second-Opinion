#include "SessionEngine.h"

#include "AudioCache.h"

#include <cstdio>
#include <limits>
#include <algorithm>
#include <cstring>

#include "bridge/Json.h"
#include "core/analysis/Analyzer.h"
#include "core/analysis/TempoEstimator.h"
#include "core/song/LocaleFreeNumber.h"
#include "core/reharm/Reharmonizer.h"
#include "core/theory/ChordNamer.h"
#include "core/theory/ChordSymbolParser.h"
#include "plugin/PluginProcessor.h"
#include "SongBridge.h"

#if SO_HAS_ONNX
 #include "core/analysis/BasicPitchRunner.h"
#endif

using juce::var;
namespace json = so::bridge::json;

namespace
{
    constexpr const char* stageIds[] = { "preparing", "transcribing", "harmony" };

   #if SO_HAS_ONNX
    /** Finds the bundled Basic Pitch model next to the plugin binary (ADR 0009 layout). */
    juce::File findModel()
    {
        const auto bin = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (const auto& candidate : { bin.getChildFile ("../Resources/basic_pitch/nmp.onnx"),
                                       bin.getChildFile ("Resources/basic_pitch/nmp.onnx"),
                                       bin.getChildFile ("basic_pitch/nmp.onnx") })
            if (candidate.existsAsFile())
                return candidate;
        return {};
    }
   #endif
}

SessionEngine::SessionEngine (SecondOpinionProcessor& p) : processor (p) {}

SessionEngine::~SessionEngine()
{
    alive->store (false);
    cancelFlag->store (true);
    pool.removeAllJobs (true, 10000);
    ioPool.removeAllJobs (true, 10000);
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------
void SessionEngine::emitError (const juce::String& code, const juce::String& message)
{
    if (emit)
        emit ("error", json::object ({ { "code", code }, { "message", message } }));
}

void SessionEngine::emitProgress (const juce::String& stage, float pct01)
{
    if (emit)
        emit ("progress", json::object ({ { "stage", stage }, { "pct", juce::jlimit (0.0f, 100.0f, pct01 * 100.0f) } }));
}

std::optional<double> SessionEngine::hostTempo() const
{
    const auto& t = processor.getTransport();
    return t.hasTempo.load() ? std::optional<double> (t.bpm.load()) : std::nullopt;
}

double SessionEngine::sessionBpm() const
{
    const auto& t = processor.getTransport();
    return t.hasTempo.load() ? t.bpm.load() : 120.0;
}

so::reharm::Settings SessionEngine::currentSettings (std::uint64_t seed) const
{
    auto& s = processor.apvts;
    auto value = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };

    so::reharm::Settings r;
    r.tension = juce::roundToInt (value (so::params::tension));
    r.smoothness = juce::roundToInt (value (so::params::smoothness));
    r.families = 0;
    for (size_t i = 0; i < so::params::colorIds.size(); ++i)
        if (value (so::params::colorIds[i]) > 0.5f)
            r.families |= 1u << i;   // same bit order as so::reharm::Family
    if (r.families == 0)
        r.families = so::reharm::familyBit (so::reharm::Family::Diatonic);
    r.bassMotion = (so::reharm::BassMotion) juce::jlimit (0, 3, juce::roundToInt (value (so::params::bassMotion)));
    r.rangeLow = juce::roundToInt (value (so::params::rangeLow));
    r.rangeHigh = juce::roundToInt (value (so::params::rangeHigh));
    r.density = juce::roundToInt (value (so::params::density));
    r.melodyOnTop = value (so::params::melodyOnTop) > 0.5f ? so::reharm::MelodyOnTop::Exact : so::reharm::MelodyOnTop::Off;
    r.seed = seed + 1;
    return r.sanitized();
}

std::set<int> SessionEngine::lockedIds() const
{
    std::set<int> ids;
    if (analysisResult)
        for (const auto& s : analysisResult->segments)
            if (s.userLocked)
                ids.insert (s.id);
    return ids;
}

// ---------------------------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------------------------
void SessionEngine::analyze (so::analysis::AudioClip clip, so::analysis::BeatGrid grid, juce::String src, juce::String name)
{
    cancelFlag->store (true);              // stop anything in flight
    cancelFlag = std::make_shared<std::atomic<bool>> (false);
    const int gen = ++jobGeneration;
    auto cancelPtr = cancelFlag;
    auto aliveFlag = alive;
    auto settings = currentSettings (0);
    auto options = analyzerOptions();

    // FR-ST-01 AC3: captured audio goes to the local cache (hash only in the project).
    auto clipPtr = std::make_shared<const so::analysis::AudioClip> (std::move (clip));
    ++analysisSerial;
    cacheHash.clear();
    if (src == "captured")
        startCacheWrite (clipPtr);

    auto post = [this, aliveFlag, gen] (std::function<void()> fn)
    {
        juce::MessageManager::callAsync ([this, aliveFlag, gen, fn = std::move (fn)]
        {
            if (aliveFlag->load() && gen == jobGeneration)
                fn();
        });
    };

    pool.addJob ([this, clipPtr, grid, src, name, cancelPtr, settings, options, post]() mutable
    {
        const auto& clip = *clipPtr;
        std::atomic<float> lastSent { -1.0f };

        auto progress = [&] (int stage, float pct)
        {
            // Throttle to steps of 2% so the bridge isn't flooded.
            const float key = (float) stage + pct;
            if (key - lastSent.load() < 0.02f && pct < 1.0f)
                return;
            lastSent = key;
            const juce::String id (stageIds[juce::jlimit (0, 2, stage)]);
            post ([this, id, pct] { emitProgress (id, pct); });
        };

       #if SO_HAS_ONNX
        // Basic Pitch notes improve the melody line and give chroma a cleaner input (ADR 0009).
        if (const auto model = findModel(); model.existsAsFile())
        {
            so::analysis::BasicPitchRunner::Config cfg;
            cfg.modelPath = model.getFullPathName().toStdString();
           #if JUCE_WINDOWS
            cfg.runtimeLibraryPath = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                         .getSiblingFile ("onnxruntime.dll").getFullPathName().toStdString();
           #endif
            so::analysis::BasicPitchRunner runner (cfg);
            if (runner.prepare())
            {
                std::atomic<float> bpProgress { 0.0f };
                progress (0, 1.0f);
                auto bp = runner.run (clip, {}, cancelPtr.get(), &bpProgress);
                if (bp.ok && ! bp.notes.empty())
                    options.notes = std::move (bp.notes);
            }
        }
       #endif

        so::analysis::AnalysisStatus status;
        auto result = so::analysis::analyze (clip, grid, options, progress, cancelPtr.get(), &status);

        if (status.cancelled || cancelPtr->load())
            return;

        if (result.segments.empty())
        {
            const auto msg = juce::String::fromUTF8 (status.message.empty()
                                 ? "Couldn't find a steady pitch, so try soloing the vocal or a cleaner part."
                                 : status.message.c_str());
            post ([this, msg] { emitError ("no_pitch", msg); });
            return;
        }

        post ([this] { emitProgress ("suggestions", 0.0f); });
        auto list = so::reharm::generate (result, settings, {});
        if (cancelPtr->load())
            return;

        post ([this, result = std::move (result), list = std::move (list), src, name, settings]() mutable
        {
            analysisResult = std::move (result);
            source = src;
            sourceName = name;
            generations.clear();
            songs.clear();
            song.reset();

            Generation g;
            g.settings = settings;
            g.input = std::make_shared<const so::analysis::AnalysisResult> (*analysisResult);
            g.list = std::move (list);
            pushGeneration (std::move (g));
            persist();

            emitProgress ("suggestions", 1.0f);
            if (emit)
            {
                emit ("analysisReady", json::analysis (*analysisResult, source, sourceName));
                emit ("suggestionsReady", suggestionsPayload());
            }
        });
    });
}

var SessionEngine::analyzePcm (const var& request)
{
    const auto rate = (double) request.getProperty ("sampleRate", 22050.0);
    const auto name = request.getProperty ("name", "Audio file").toString();
    auto text = std::make_shared<juce::String> (request.getProperty ("samplesBase64", {}).toString());

    if (rate < 8000.0 || rate > 192000.0 || text->isEmpty())
    {
        emitError ("decode_failed", "That audio couldn't be read. Try a WAV, AIFF, FLAC or MP3 file.");
        return json::ack (false, "Unsupported audio");
    }

    // ~4 bytes per sample after decoding; 3 minutes is the limit (contract).
    const double approxSeconds = (double) text->length() * 0.75 / 4.0 / rate;
    if (approxSeconds > 181.0)
    {
        emitError ("too_long", "That file is longer than 3 minutes. Trim it to the idea you want to build on.");
        return json::ack (false, "Too long");
    }

    auto aliveFlag = alive;
    const auto bpm = sessionBpm();
    pool.addJob ([this, text, rate, name, aliveFlag, bpm]
    {
        juce::MemoryOutputStream out;
        const bool ok = juce::Base64::convertFromBase64 (out, *text);
        const auto numSamples = ok ? out.getDataSize() / sizeof (float) : 0;

        std::vector<float> samples (numSamples);
        if (numSamples > 0)
            std::memcpy (samples.data(), out.getData(), numSamples * sizeof (float));   // LE on every target platform

        auto result = AudioFileLoader::clipFromPcm (samples.data(), samples.size(), rate, AudioFileLoader::maxSecondsFor (bpm));

        juce::MessageManager::callAsync ([this, aliveFlag, result = std::move (result), name, bpm]() mutable
        {
            if (! aliveFlag->load())
                return;
            if (! result.ok || result.clip.samples.size() < (size_t) result.clip.sampleRate)
            {
                emitError ("decode_failed", "That audio is too short or couldn't be read. Use at least one second.");
                return;
            }
            const auto grid = so::analysis::chooseFileGrid (result.clip, hostTempo());
            so::analysis::trimTrailingTail (result.clip, grid);
            analyze (std::move (result.clip), grid, "file", name);
        });
    });

    return json::ack (true);
}

void SessionEngine::openAudioFile (std::function<void (var)> complete)
{
    chooser = std::make_unique<juce::FileChooser> ("Open an audio idea",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    auto aliveFlag = alive;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, aliveFlag, complete] (const juce::FileChooser& fc)
    {
        if (! aliveFlag->load())
            return;

        const auto file = fc.getResult();
        if (! file.existsAsFile())
        {
            complete (json::object ({ { "ok", false }, { "cancelled", true } }));
            return;
        }

        complete (json::object ({ { "ok", true }, { "name", file.getFileName() } }));
        emitProgress ("preparing", 0.0f);

        const auto bpm = sessionBpm();
        fileLoader.load (file, AudioFileLoader::maxSecondsFor (bpm), [this, aliveFlag, name = file.getFileName(), bpm] (AudioFileLoader::Result r)
        {
            if (! aliveFlag->load() || r.cancelled)
                return;
            if (! r.ok)
            {
                emitError ("decode_failed", r.error.isNotEmpty() ? r.error : juce::String ("That file couldn't be read."));
                return;
            }
            const auto grid = so::analysis::chooseFileGrid (r.clip, hostTempo());
            so::analysis::trimTrailingTail (r.clip, grid);
            analyze (std::move (r.clip), grid, "file", name);
        });
    });
}

var SessionEngine::cancel()
{
    cancelFlag->store (true);
    ++jobGeneration;
    fileLoader.cancel();
    aiClient.cancel();
    if (processor.captureState() != SecondOpinionProcessor::CaptureState::Idle)
        processor.stopCapture();
    return json::ack (true);
}

// ---------------------------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------------------------
var SessionEngine::startCapture()
{
    pendingClip.reset();
    processor.armCapture();
    emitCaptureState ("armed");
    return json::ack (true);
}

var SessionEngine::stopCapture()
{
    processor.stopCapture();
    return json::ack (true);
}

var SessionEngine::setTempo (double bpm)
{
    if (! pendingClip || bpm < 40.0 || bpm > 240.0)
        return json::ack (false, "Set a tempo between 40 and 240 BPM.");

    auto [clip, grid] = std::move (*pendingClip);
    pendingClip.reset();
    grid.bpm = bpm;
    grid.downbeatSec = 0.0;   // AC5: downbeat taken from the capture start
    analyze (std::move (clip), grid, "captured", "Captured audio");
    return json::ack (true);
}

void SessionEngine::emitCaptureState (const juce::String& state)
{
    lastCaptureState = state;
    if (emit)
        emit ("captureState", json::object ({
            { "state", state },
            { "bar", juce::jmax (1, (int) std::ceil (processor.captureProgressBars())) },
            { "level", juce::jlimit (0.0f, 1.0f, processor.inputLevel()) },
            { "needsTempo", pendingClip.has_value() },
            { "maxBars", 64 },
        }));
}

void SessionEngine::tick()
{
    using CS = SecondOpinionProcessor::CaptureState;
    const auto cs = processor.captureState();

    if (cs == CS::Armed)
        emitCaptureState ("armed");
    else if (cs == CS::Recording)
        emitCaptureState ("recording");
    else if (cs == CS::Done)
    {
        if (auto taken = processor.takeCapturedClip())
        {
            const auto bars = taken->first.samples.size() / std::max (1.0, taken->first.sampleRate)
                              * taken->second.bpm / 60.0 / taken->second.beatsPerBar;
            if (bars < 1.0)
            {
                emitCaptureState ("idle");
                emitError ("capture_failed", "That capture was shorter than a bar. Play a little longer, then press Stop.");
            }
            else if (processor.needsTempo())
            {
                pendingClip = std::move (*taken);
                emitCaptureState ("done");
                emitError ("no_tempo", "Your DAW didn't send a tempo. Set the BPM so bar lines match your session.");
            }
            else
            {
                emitCaptureState ("done");
                analyze (std::move (taken->first), taken->second, "captured", "Captured audio");
            }
        }
    }
    else if (lastCaptureState == "armed" || lastCaptureState == "recording")
    {
        emitCaptureState ("idle");   // disarmed or aborted
    }

    // Audition playhead, about 20 per second, then one final "stopped".
    const bool playing = processor.isAuditioning();
    if (emit && (playing || wasAuditioning))
        emit ("auditionState", json::object ({
            { "playing", playing },
            { "beat", processor.auditionBeat() },
            { "target", auditionTarget },
            { "id", auditionId },
        }));
    wasAuditioning = playing;
}

// ---------------------------------------------------------------------------------------------
// Corrections, locks, generation
// ---------------------------------------------------------------------------------------------
var SessionEngine::setChord (const juce::String& segmentId, const juce::String& symbol)
{
    if (! analysisResult)
        return json::ack (false, "Analyze some audio first.");

    const auto parsed = so::theory::ChordSymbolParser::parse (symbol.toStdString());
    if (const auto* err = std::get_if<so::theory::ParseError> (&parsed))
        return json::ack (false, json::str (err->message));

    for (auto& s : analysisResult->segments)
    {
        if (juce::String (s.id) == segmentId)
        {
            s.chord = std::get<so::theory::Chord> (parsed);
            s.confidence = 1.0f;
            s.userCorrected = true;
            s.userLocked = true;
            persist();
            return json::object ({ { "ok", true }, { "symbol", json::symbol (s.chord) } });
        }
    }
    return json::ack (false, "That chord is no longer in the timeline.");
}

var SessionEngine::lock (const var& segmentIds, bool locked)
{
    if (! analysisResult)
        return json::ack (false);

    juce::StringArray ids;
    if (const auto* arr = segmentIds.getArray())
        for (const auto& v : *arr)
            ids.add (v.toString());

    for (auto& s : analysisResult->segments)
        if (ids.contains (juce::String (s.id)))
            s.userLocked = locked;

    persist();
    return json::ack (true);
}

var SessionEngine::generate (std::uint64_t seed)
{
    if (! analysisResult)
    {
        emitError ("generate_failed", "Capture or open some audio first.");
        return json::ack (false);
    }

    const int gen = ++jobGeneration;
    auto aliveFlag = alive;
    auto analysisCopy = std::make_shared<const so::analysis::AnalysisResult> (*analysisResult);
    auto settings = currentSettings (seed);
    auto locked = lockedIds();

    pool.addJob ([this, aliveFlag, gen, analysisCopy, settings, locked, seed]
    {
        auto list = so::reharm::generate (*analysisCopy, settings, locked);
        juce::MessageManager::callAsync ([this, aliveFlag, gen, list = std::move (list), seed, analysisCopy, settings, locked]() mutable
        {
            if (! aliveFlag->load() || gen != jobGeneration)
                return;
            Generation g;
            g.seed = seed;
            g.settings = settings;
            g.locks = locked;
            g.input = analysisCopy;
            g.list = std::move (list);
            pushGeneration (std::move (g));
            persist();
            if (emit)
                emit ("suggestionsReady", suggestionsPayload());
        });
    });

    return json::ack (true);
}

// ---------------------------------------------------------------------------------------------
// Audition
// ---------------------------------------------------------------------------------------------
var SessionEngine::audition (const var& request)
{
    const auto target = request.getProperty ("target", "suggestion").toString();
    const auto id = request.getProperty ("id", {}).toString();
    const bool play = (bool) request.getProperty ("play", true);

    if (! play)
    {
        processor.stopAudition();
        return json::ack (true);
    }

    std::vector<so::midi::MidiLane> lanes;
    double bpm = analysisResult ? analysisResult->grid.bpm : sessionBpm();
    double lengthBeats = analysisResult ? analysisResult->bars * analysisResult->grid.beatsPerBar : 0.0;

    if (target == "original" && analysisResult)
    {
        lanes = so::reharm::voiceOriginal (*analysisResult, currentSettings (suggestionSeed), lockedIds()).laneList();
    }
    else if (target == "suggestion")
    {
        for (const auto& s : suggestionList)
            if (json::str (s.id) == id)
                lanes = s.laneList();
    }
    else if ((target == "section" || target == "song") && song != nullptr)
    {
        auto arrangement = SongBridge::lanes (*song, target, id);
        lanes = std::move (arrangement.lanes);
        bpm = arrangement.bpm;
        lengthBeats = arrangement.lengthBeats;
    }

    if (lanes.empty() || lengthBeats <= 0.0)
        return json::ack (false, "Nothing to play yet.");

    processor.setPreviewSnapshot (lanes, bpm, lengthBeats);
    processor.startAudition();
    auditionTarget = target;
    auditionId = id;
    return json::ack (true);
}

// ---------------------------------------------------------------------------------------------
// Build out
// ---------------------------------------------------------------------------------------------
var SessionEngine::aiStatus() const
{
    auto status = json::object ({ { "hasKey", AiMoodClient::hasKey() }, { "maskedKey", AiMoodClient::maskedKey() } });
    if (aiLastError.isNotEmpty())
        status.getDynamicObject()->setProperty ("lastError", aiLastError);
    return status;
}

var SessionEngine::buildSong (const var& request)
{
    const bool useAi = (bool) request.getProperty ("useOnlineAi", false) && AiMoodClient::hasKey();
    const auto description = request.getProperty ("description", {}).toString();

    emitProgress ("song", 0.05f);

    if (useAi && description.trim().isNotEmpty())
    {
        auto aliveFlag = alive;
        const auto parsed = SongBridge::parseRequest (request);
        aiClient.interpret (juce::String::fromUTF8 (SongBridge::aiUserPrompt (parsed).c_str()),
                            juce::String::fromUTF8 (SongBridge::aiSystemPrompt().c_str()),
                            [this, aliveFlag, request] (const AiMoodClient::Result& r)
        {
            if (! aliveFlag->load())
                return;
            aiLastError = r.ok ? juce::String() : r.error;
            if (! r.ok)
                emitError (r.error.contains ("rejected") ? "ai_key_invalid" : "ai_unavailable", r.error);
            runSongBuild (request, r.ok ? r.json : juce::String(), ! r.ok);
        });
        return json::ack (true);
    }

    runSongBuild (request, {}, false);
    return json::ack (true);
}

void SessionEngine::runSongBuild (const var& request, juce::String aiJson, bool aiFailed)
{
    const int gen = ++jobGeneration;
    auto aliveFlag = alive;
    auto analysisCopy = analysisResult ? std::optional<so::analysis::AnalysisResult> (*analysisResult) : std::nullopt;
    const auto bpm = analysisResult ? analysisResult->grid.bpm : sessionBpm();
    auto req = SongBridge::parseRequest (request);
    juce::ignoreUnused (aiFailed);

    emitProgress ("song", 0.2f);

    pool.addJob ([this, aliveFlag, gen, analysisCopy = std::move (analysisCopy), req, aiJson, bpm, request]
    {
        auto built = SongBridge::build (analysisCopy, req, aiJson.toStdString(), bpm);

        juce::MessageManager::callAsync ([this, aliveFlag, gen, built = std::move (built), analysisCopy, aiJson, bpm, request]() mutable
        {
            if (! aliveFlag->load() || gen != jobGeneration)
                return;
            if (built.song == nullptr)
            {
                emitError ("song_failed", built.error.isNotEmpty() ? built.error : juce::String ("The song couldn't be built. Try again."));
                return;
            }
            SongRecord rec;
            rec.request = request;
            rec.aiJson = built.meta.interpreter == "online" ? aiJson : juce::String();
            rec.bpm = bpm;
            if (analysisCopy)
                rec.idea = std::make_shared<const so::analysis::AnalysisResult> (*analysisCopy);
            rec.song = std::move (built.song);
            rec.meta = built.meta;
            adoptSong (std::move (rec));
            persist();
            emitProgress ("song", 1.0f);
            if (emit)
                emit ("songReady", songPayload (false));
        });
    });
}

var SessionEngine::chooseSectionOption (int sectionIndex, int optionIndex)
{
    if (song == nullptr || ! SongBridge::choose (*song, sectionIndex, optionIndex))
        return json::ack (false);
    persist();
    return json::ack (true);
}

void SessionEngine::replay()
{
    if (! emit)
        return;
    if (analysisResult)
    {
        emit ("analysisReady", json::analysis (*analysisResult, source, sourceName));
        auto payload = suggestionsPayload();
        const auto selected = processor.session.getProperty ("ui_selected", {}).toString();
        if (selected.isNotEmpty())
            payload.getDynamicObject()->setProperty ("selectedId", selected);
        emit ("suggestionsReady", payload);
    }
    if (song != nullptr)
        emit ("songReady", songPayload (true));
}

// ---------------------------------------------------------------------------------------------
// History, payloads
// ---------------------------------------------------------------------------------------------
void SessionEngine::pushGeneration (Generation g)
{
    g.id = nextId++;
    suggestionList = g.list;
    suggestionSeed = g.seed;
    currentGenId = g.id;
    generations.push_back (std::move (g));
    while (generations.size() > maxHistory)
        generations.pop_front();
}

SessionEngine::Generation* SessionEngine::findGeneration (int id)
{
    for (auto& g : generations)
        if (g.id == id)
            return &g;
    return nullptr;
}

void SessionEngine::adoptSong (SongRecord rec)
{
    rec.id = nextId++;
    song = rec.song;
    songMeta = rec.meta;
    currentSongId = rec.id;
    songs.push_back (std::move (rec));
    while (songs.size() > maxHistory)
        songs.pop_front();
}

void SessionEngine::clearSession()
{
    ++jobGeneration;
    analysisResult.reset();
    source = "captured";
    sourceName.clear();
    suggestionList.clear();
    suggestionSeed = 0;
    generations.clear();
    songs.clear();
    song.reset();
    songMeta = {};
    currentGenId = currentSongId = 0;
    cacheHash.clear();
}

juce::var SessionEngine::suggestionsPayload() const
{
    auto payload = json::suggestions (suggestionList, suggestionSeed);
    payload.getDynamicObject()->setProperty ("generation", currentGenId);
    return payload;
}

juce::var SessionEngine::songPayload (bool restored) const
{
    if (song == nullptr)
        return {};
    auto payload = SongBridge::toJson (*song, songMeta);
    payload.getDynamicObject()->setProperty ("generation", currentSongId);
    if (restored)
        payload.getDynamicObject()->setProperty ("restored", true);
    return payload;
}

std::vector<std::uint64_t> SessionEngine::seedHistory() const
{
    std::vector<std::uint64_t> seeds;
    for (const auto& g : generations)
        seeds.push_back (g.seed);
    if (seeds.size() > savedGenerations)
        seeds.erase (seeds.begin(), seeds.end() - (std::ptrdiff_t) savedGenerations);
    return seeds;
}

void SessionEngine::adoptAnalysis (so::analysis::AnalysisResult result, const juce::String& src, const juce::String& name)
{
    ++jobGeneration;
    const auto settings = currentSettings (0);
    auto list = so::reharm::generate (result, settings, {});

    analysisResult = std::move (result);
    source = src;
    sourceName = name;
    generations.clear();
    songs.clear();
    song.reset();
    currentSongId = 0;

    Generation g;
    g.settings = settings;
    g.input = std::make_shared<const so::analysis::AnalysisResult> (*analysisResult);
    g.list = std::move (list);
    pushGeneration (std::move (g));
    persist();
}

bool SessionEngine::generateNow (std::uint64_t seed)
{
    if (! analysisResult)
        return false;
    ++jobGeneration;
    Generation g;
    g.seed = seed;
    g.settings = currentSettings (seed);
    g.locks = lockedIds();
    g.input = std::make_shared<const so::analysis::AnalysisResult> (*analysisResult);
    g.list = so::reharm::generate (*g.input, g.settings, g.locks);
    pushGeneration (std::move (g));
    persist();
    return true;
}

bool SessionEngine::buildSongNow (const var& request, const juce::String& aiJson)
{
    ++jobGeneration;
    const auto bpm = analysisResult ? analysisResult->grid.bpm : sessionBpm();
    auto built = SongBridge::build (analysisResult, SongBridge::parseRequest (request), aiJson.toStdString(), bpm);
    if (built.song == nullptr)
        return false;

    SongRecord rec;
    rec.request = request;
    rec.aiJson = built.meta.interpreter == "online" ? aiJson : juce::String();
    rec.bpm = bpm;
    if (analysisResult)
        rec.idea = std::make_shared<const so::analysis::AnalysisResult> (*analysisResult);
    rec.song = std::move (built.song);
    rec.meta = built.meta;
    adoptSong (std::move (rec));
    persist();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Settings (FR-SET) and the audio cache
// ---------------------------------------------------------------------------------------------
so::analysis::AnalyzerOptions SessionEngine::analyzerOptions() const
{
    so::analysis::AnalyzerOptions o;
    o.quality = prefs.quality;
    o.source = prefs.source;
    return o;
}

namespace
{
    var settingsObject (const var& v)
    {
        if (v.isString())
            return juce::JSON::parse (v.toString());
        return v;
    }
}

var SessionEngine::setSettings (const var& raw)
{
    const auto v = settingsObject (raw);
    if (v.getDynamicObject() == nullptr)
        return json::ack (false, "Settings must be an object.");

    using Q = so::analysis::AnalysisQuality;
    using S = so::analysis::SourceHint;

    if (v.hasProperty ("quality"))
    {
        const auto q = v["quality"].toString();
        prefs.quality = q == "fast" ? Q::Fast : q == "accurate" ? Q::Accurate : Q::Balanced;
    }
    if (v.hasProperty ("sourceHint"))
    {
        const auto h = v["sourceHint"].toString();
        prefs.source = h == "vocal" ? S::Vocal : h == "keys" ? S::Keys : h == "guitar" ? S::Guitar
                     : h == "loop" ? S::Loop : S::Auto;
    }
    if (v.hasProperty ("ppq"))
        prefs.ppq = (int) v["ppq"] == 480 ? 480 : 960;
    if (v.hasProperty ("velocity"))
        prefs.humanize = v["velocity"].toString() != "flat";
    if (v.hasProperty ("previewSound"))
    {
        using PS = SecondOpinionProcessor::PreviewSound;
        const auto p = v["previewSound"].toString();
        processor.setPreviewSound (p == "pad" ? PS::Pad : p == "off" ? PS::Off : PS::Keys);
    }

    {
        const juce::ScopedLock sl (processor.sessionLock);
        processor.session.setProperty ("ui_settings", juce::JSON::toString (v, true), nullptr);
    }
    return json::ack (true);
}

void SessionEngine::loadSettingsFromSession()
{
    prefs = {};
    const auto text = processor.session.getProperty ("ui_settings", {}).toString();
    if (text.isNotEmpty())
        setSettings (text);
}

var SessionEngine::clearCache()
{
    const int removed = so::cache::clear();
    return json::object ({ { "ok", true }, { "removed", removed } });
}

void SessionEngine::startCacheWrite (std::shared_ptr<const so::analysis::AudioClip> clip)
{
    auto aliveFlag = alive;
    const int serial = analysisSerial;
    ioPool.addJob ([this, aliveFlag, serial, clip]
    {
        const auto hash = so::cache::hashClip (*clip);
        so::cache::writeClip (*clip, hash, so::cache::cacheDirectory());
        juce::MessageManager::callAsync ([this, aliveFlag, serial, hash]
        {
            if (! aliveFlag->load() || serial != analysisSerial)
                return;
            cacheHash = hash;
            persist();
        });
    });
}

// ---------------------------------------------------------------------------------------------
// Undo / redo (FR-ST-02)
// ---------------------------------------------------------------------------------------------
var SessionEngine::restoreSnapshot (const var& snap)
{
    bool ok = true;

    if (analysisResult)
    {
        if (const auto* segs = snap.getProperty ("segments", {}).getArray())
        {
            for (const auto& v : *segs)
            {
                const auto id = v.getProperty ("id", {}).toString();
                for (auto& s : analysisResult->segments)
                {
                    if (juce::String (s.id) != id)
                        continue;
                    if (v.hasProperty ("symbol"))
                    {
                        if (auto chord = so::theory::ChordSymbolParser::tryParse (v["symbol"].toString().toStdString()))
                        {
                            if (! (*chord == s.chord))
                                s.chord = *chord;
                        }
                        else
                            ok = false;
                    }
                    if (v.hasProperty ("confidence"))
                        s.confidence = juce::jlimit (0.0f, 1.0f, (float) (double) v["confidence"]);
                    if (v.hasProperty ("userLocked"))
                        s.userLocked = (bool) v["userLocked"];
                    if (v.hasProperty ("userCorrected"))
                        s.userCorrected = (bool) v["userCorrected"];
                }
            }
        }
    }

    const int genId = (int) snap.getProperty ("generation", 0);
    if (genId > 0 && genId != currentGenId)
    {
        if (auto* g = findGeneration (genId))
        {
            if (g->list.empty() && analysisResult)
                g->list = so::reharm::generate (g->input ? *g->input : *analysisResult, g->settings, g->locks);
            ++jobGeneration;   // a generate still in flight would overwrite the restored set
            suggestionList = g->list;
            suggestionSeed = g->seed;
            currentGenId = g->id;
        }
        else
            ok = false;
    }

    if (snap.hasProperty ("song"))
    {
        const auto s = snap["song"];
        if (s.isVoid() || s.isUndefined() || (s.getDynamicObject() == nullptr && ! s.isObject()))
        {
            song.reset();
            currentSongId = 0;
        }
        else
        {
            const int songId = (int) s.getProperty ("generation", 0);
            if (songId > 0 && songId != currentSongId)
            {
                const SongRecord* found = nullptr;
                for (const auto& r : songs)
                    if (r.id == songId)
                        found = &r;
                if (found != nullptr)
                {
                    song = found->song;
                    songMeta = found->meta;
                    currentSongId = found->id;
                }
                else
                    ok = false;
            }
            if (song != nullptr)
                if (const auto* chosen = s.getProperty ("chosen", {}).getArray())
                    for (int i = 0; i < juce::jmin (chosen->size(), (int) song->sections.size()); ++i)
                        if (song->sections[(size_t) i].repeatOf < 0 && song->sections[(size_t) i].chosen != (int) (*chosen)[i])
                            SongBridge::choose (*song, i, (int) (*chosen)[i]);
        }
    }

    persist();
    return json::ack (ok);
}

// ---------------------------------------------------------------------------------------------
// Save and recall (FR-ST-01, docs/adr/0011-session-persistence.md)
// ---------------------------------------------------------------------------------------------
namespace
{
    // Every number is written by hand: %.17g round-trips doubles exactly, JUCE's var does not above 1e6.
    juce::String num (double v)
    {
        return juce::String (so::text::formatRoundTrip (v));
    }

    std::optional<double> readNum (const juce::ValueTree& t, const juce::Identifier& id)
    {
        if (! t.hasProperty (id))
            return std::nullopt;
        const auto s = t.getProperty (id).toString().trim();
        if (s.isEmpty() || ! s.containsOnly ("0123456789+-.eE"))
            return std::nullopt;
        const double v = s.getDoubleValue();
        return std::isfinite (v) ? std::optional<double> (v) : std::nullopt;
    }

    std::optional<int> readInt (const juce::ValueTree& t, const juce::Identifier& id, int lo, int hi)
    {
        const auto v = readNum (t, id);
        if (! v || *v != std::floor (*v) || *v < lo || *v > hi)
            return std::nullopt;
        return (int) *v;
    }

    std::uint64_t readU64 (const juce::ValueTree& t, const juce::Identifier& id)
    {
        const auto s = t.getProperty (id).toString().trim();
        if (s.isEmpty() || ! s.containsOnly ("0123456789") || s.length() > 18)
            return 0;
        return (std::uint64_t) s.getLargeIntValue();
    }

    juce::String pitchCode (const so::theory::SpelledPitch& p)
    {
        return juce::String ((int) p.letter) + "," + juce::String (p.accidental);
    }

    juce::String chordCode (const so::theory::Chord& c)
    {
        auto s = pitchCode (c.root) + "," + juce::String ((int) c.quality);
        if (c.bass)
            s << "," << pitchCode (*c.bass);
        return s;
    }

    std::optional<so::theory::SpelledPitch> pitchFrom (const juce::StringArray& t, int i)
    {
        if (i + 1 >= t.size() || ! t[i].containsOnly ("0123456789") || ! t[i + 1].containsOnly ("-0123456789")
            || t[i].isEmpty() || t[i + 1].isEmpty())
            return std::nullopt;
        const int letter = t[i].getIntValue(), acc = t[i + 1].getIntValue();
        if (letter < 0 || letter > 6 || acc < -2 || acc > 2)
            return std::nullopt;
        return so::theory::SpelledPitch { (so::theory::Letter) letter, acc };
    }

    std::optional<so::theory::Chord> chordFrom (const juce::String& code)
    {
        const auto t = juce::StringArray::fromTokens (code, ",", "");
        if (t.size() != 3 && t.size() != 5)
            return std::nullopt;
        auto root = pitchFrom (t, 0);
        if (! root || t[2].isEmpty() || ! t[2].containsOnly ("0123456789"))
            return std::nullopt;
        const int q = t[2].getIntValue();
        if (q < 0 || q >= so::theory::numChordQualities)
            return std::nullopt;
        so::theory::Chord c;
        c.root = *root;
        c.quality = (so::theory::ChordQuality) q;
        if (t.size() == 5)
        {
            auto bass = pitchFrom (t, 3);
            if (! bass)
                return std::nullopt;
            c.bass = *bass;
        }
        return c;
    }

    // Melody: "t1:" + base64 of zigzag varints (pitch delta, start delta, length), ticks of 1/960
    // beat, delta-encoded (PRD 7.7). Falls back to "d:" text doubles when a value is off that grid.
    constexpr double ticksPerBeat = 960.0;

    void putVarint (juce::MemoryOutputStream& out, std::int64_t v)
    {
        auto z = (std::uint64_t) ((v << 1) ^ (v >> 63));
        while (z >= 0x80)
        {
            out.writeByte ((char) (std::uint8_t) (z | 0x80));
            z >>= 7;
        }
        out.writeByte ((char) (std::uint8_t) z);
    }

    bool getVarint (const std::uint8_t*& p, const std::uint8_t* end, std::int64_t& v)
    {
        std::uint64_t z = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            if (p >= end)
                return false;
            const auto b = *p++;
            z |= (std::uint64_t) (b & 0x7f) << shift;
            if ((b & 0x80) == 0)
            {
                v = (std::int64_t) (z >> 1) ^ -(std::int64_t) (z & 1);
                return true;
            }
        }
        return false;
    }

    bool onGrid (double beats, std::int64_t& ticks)
    {
        if (std::abs (beats) > 1.0e9)
            return false;
        ticks = (std::int64_t) std::llround (beats * ticksPerBeat);
        return (double) ticks / ticksPerBeat == beats;
    }

    juce::String encodeMelody (const std::vector<so::analysis::MelodyNote>& melody)
    {
        juce::MemoryOutputStream out;
        std::int64_t prevPitch = 60, prevStart = 0;
        bool exact = true;
        for (const auto& n : melody)
        {
            std::int64_t start = 0, len = 0;
            if (! onGrid (n.startBeat, start) || ! onGrid (n.beats, len))
            {
                exact = false;
                break;
            }
            putVarint (out, n.pitch - prevPitch);
            putVarint (out, start - prevStart);
            putVarint (out, len);
            prevPitch = n.pitch;
            prevStart = start;
        }
        if (exact)
            return "t1:" + juce::Base64::toBase64 (out.getData(), out.getDataSize());

        juce::String s ("d:");
        for (const auto& n : melody)
            s << n.pitch << "," << num (n.startBeat) << "," << num (n.beats) << ";";
        return s;
    }

    std::optional<std::vector<so::analysis::MelodyNote>> decodeMelody (const juce::String& text)
    {
        constexpr size_t maxNotes = 100000;
        std::vector<so::analysis::MelodyNote> out;
        auto valid = [] (const so::analysis::MelodyNote& n)
        {
            return n.pitch >= 0 && n.pitch <= 127 && std::isfinite (n.startBeat) && std::isfinite (n.beats)
                && n.startBeat >= 0.0 && n.beats > 0.0 && n.startBeat < 1.0e6 && n.beats < 1.0e6;
        };

        if (text.isEmpty())
            return out;

        if (text.startsWith ("t1:"))
        {
            juce::MemoryOutputStream bytes;
            if (! juce::Base64::convertFromBase64 (bytes, text.substring (3)))
                return std::nullopt;
            auto* p = static_cast<const std::uint8_t*> (bytes.getData());
            const auto* end = p + bytes.getDataSize();
            std::int64_t pitch = 60, start = 0;
            while (p < end)
            {
                std::int64_t dp = 0, ds = 0, len = 0;
                if (! getVarint (p, end, dp) || ! getVarint (p, end, ds) || ! getVarint (p, end, len) || out.size() >= maxNotes)
                    return std::nullopt;
                if (std::abs (dp) > 256 || std::abs (ds) > (std::int64_t) 1.0e12 || std::abs (len) > (std::int64_t) 1.0e12)
                    return std::nullopt;
                pitch += dp;
                start += ds;
                so::analysis::MelodyNote n { (int) juce::jlimit<std::int64_t> (-1, 128, pitch), (double) start / ticksPerBeat, (double) len / ticksPerBeat };
                if (! valid (n))
                    return std::nullopt;
                out.push_back (n);
            }
            return out;
        }

        if (text.startsWith ("d:"))
        {
            for (const auto& item : juce::StringArray::fromTokens (text.substring (2), ";", ""))
            {
                if (item.isEmpty())
                    continue;
                const auto f = juce::StringArray::fromTokens (item, ",", "");
                if (f.size() != 3 || out.size() >= maxNotes)
                    return std::nullopt;
                for (const auto& x : f)
                    if (x.isEmpty() || ! x.containsOnly ("0123456789+-.eE"))
                        return std::nullopt;
                so::analysis::MelodyNote n { f[0].getIntValue(), f[1].getDoubleValue(), f[2].getDoubleValue() };
                if (! valid (n))
                    return std::nullopt;
                out.push_back (n);
            }
            return out;
        }
        return std::nullopt;
    }

    juce::ValueTree settingsToTree (const so::reharm::Settings& s)
    {
        juce::ValueTree t ("Settings");
        t.setProperty ("tension", s.tension, nullptr);
        t.setProperty ("smoothness", s.smoothness, nullptr);
        t.setProperty ("families", (int) s.families, nullptr);
        t.setProperty ("bassMotion", (int) s.bassMotion, nullptr);
        t.setProperty ("pedalPc", s.pedalPc, nullptr);
        t.setProperty ("pedalBars", s.pedalBars, nullptr);
        t.setProperty ("rangeLow", s.rangeLow, nullptr);
        t.setProperty ("rangeHigh", s.rangeHigh, nullptr);
        t.setProperty ("bassLow", s.bassLow, nullptr);
        t.setProperty ("bassHigh", s.bassHigh, nullptr);
        t.setProperty ("density", s.density, nullptr);
        t.setProperty ("melodyOnTop", (int) s.melodyOnTop, nullptr);
        t.setProperty ("seed", juce::String ((juce::uint64) s.seed), nullptr);
        t.setProperty ("maxSuggestions", s.maxSuggestions, nullptr);
        return t;
    }

    so::reharm::Settings settingsFromTree (const juce::ValueTree& t)
    {
        so::reharm::Settings s;
        auto get = [&] (const char* id, int lo, int hi, int fallback) { return readInt (t, id, lo, hi).value_or (fallback); };
        s.tension = get ("tension", 0, 100, s.tension);
        s.smoothness = get ("smoothness", 0, 100, s.smoothness);
        s.families = (unsigned) get ("families", 0, (int) so::reharm::allFamilies, (int) s.families);
        s.bassMotion = (so::reharm::BassMotion) get ("bassMotion", 0, 3, 0);
        s.pedalPc = get ("pedalPc", -1, 11, s.pedalPc);
        s.pedalBars = get ("pedalBars", 1, 64, s.pedalBars);
        s.rangeLow = get ("rangeLow", 0, 127, s.rangeLow);
        s.rangeHigh = get ("rangeHigh", 0, 127, s.rangeHigh);
        s.bassLow = get ("bassLow", 0, 127, s.bassLow);
        s.bassHigh = get ("bassHigh", 0, 127, s.bassHigh);
        s.density = get ("density", 3, 6, s.density);
        s.melodyOnTop = (so::reharm::MelodyOnTop) get ("melodyOnTop", 0, 3, 0);
        s.seed = readU64 (t, "seed");
        s.maxSuggestions = get ("maxSuggestions", 6, 12, s.maxSuggestions);
        if (s.families == 0)
            s.families = so::reharm::familyBit (so::reharm::Family::Diatonic);
        return s.sanitized();
    }

    juce::String idList (const std::set<int>& ids)
    {
        juce::StringArray a;
        for (int id : ids)
            a.add (juce::String (id));
        return a.joinIntoString (",");
    }

    std::set<int> idsFrom (const juce::String& s)
    {
        std::set<int> ids;
        for (const auto& t : juce::StringArray::fromTokens (s, ",", ""))
            if (t.isNotEmpty() && t.containsOnly ("-0123456789") && t.length() < 10 && ids.size() < 4096)
                ids.insert (t.getIntValue());
        return ids;
    }

    bool sameTree (const juce::ValueTree& a, const juce::ValueTree& b) { return a.isEquivalentTo (b); }
}

juce::ValueTree SessionEngine::analysisToTree (const so::analysis::AnalysisResult& a)
{
    juce::ValueTree t ("Analysis");
    t.setProperty ("bpm", num (a.grid.bpm), nullptr);
    t.setProperty ("beatsPerBar", a.grid.beatsPerBar, nullptr);
    t.setProperty ("downbeatSec", num (a.grid.downbeatSec), nullptr);
    t.setProperty ("bars", a.bars, nullptr);
    t.setProperty ("key", pitchCode (a.key.tonic) + "," + juce::String ((int) a.key.mode), nullptr);
    t.setProperty ("keyConfidence", num (a.keyConfidence), nullptr);
    t.setProperty ("melody", encodeMelody (a.melody), nullptr);

    for (const auto& s : a.segments)
    {
        juce::ValueTree seg ("Seg");
        seg.setProperty ("id", s.id, nullptr);
        seg.setProperty ("start", num (s.startBeat), nullptr);
        seg.setProperty ("beats", num (s.beats), nullptr);
        seg.setProperty ("chord", chordCode (s.chord), nullptr);
        seg.setProperty ("confidence", num (s.confidence), nullptr);
        if (s.userLocked)
            seg.setProperty ("locked", 1, nullptr);
        if (s.userCorrected)
            seg.setProperty ("corrected", 1, nullptr);

        juce::StringArray alts;
        for (const auto& alt : s.alternatives)
            alts.add (chordCode (alt.chord) + ":" + num (alt.confidence));
        if (! alts.isEmpty())
            seg.setProperty ("alts", alts.joinIntoString (";"), nullptr);
        t.appendChild (seg, nullptr);
    }
    return t;
}

std::optional<so::analysis::AnalysisResult> SessionEngine::analysisFromTree (const juce::ValueTree& t)
{
    if (! t.isValid() || ! t.hasType ("Analysis") || t.getNumChildren() > 4096)
        return std::nullopt;

    so::analysis::AnalysisResult a;
    const auto bpm = readNum (t, "bpm");
    const auto bpb = readInt (t, "beatsPerBar", 1, 16);
    const auto downbeat = readNum (t, "downbeatSec");
    const auto bars = readInt (t, "bars", 1, 1024);
    const auto keyConf = readNum (t, "keyConfidence");
    if (! bpm || *bpm < 20.0 || *bpm > 400.0 || ! bpb || ! downbeat || std::abs (*downbeat) > 3600.0 || ! bars || ! keyConf)
        return std::nullopt;

    a.grid.bpm = *bpm;
    a.grid.beatsPerBar = *bpb;
    a.grid.downbeatSec = *downbeat;
    a.bars = *bars;
    a.keyConfidence = (float) juce::jlimit (0.0, 1.0, *keyConf);

    const auto keyTokens = juce::StringArray::fromTokens (t.getProperty ("key").toString(), ",", "");
    const auto tonic = pitchFrom (keyTokens, 0);
    if (keyTokens.size() != 3 || ! tonic || (keyTokens[2] != "0" && keyTokens[2] != "1"))
        return std::nullopt;
    a.key.tonic = *tonic;
    a.key.mode = keyTokens[2] == "1" ? so::theory::Mode::Minor : so::theory::Mode::Major;

    auto melody = decodeMelody (t.getProperty ("melody").toString());
    if (! melody)
        return std::nullopt;
    a.melody = std::move (*melody);

    const double totalBeats = (double) a.bars * a.grid.beatsPerBar;
    double lastStart = 0.0;
    std::set<int> ids;
    for (const auto& seg : t)
    {
        if (! seg.hasType ("Seg"))
            return std::nullopt;
        so::analysis::ChordSegment s;
        const auto id = readInt (seg, "id", 0, 1000000);
        const auto start = readNum (seg, "start");
        const auto beats = readNum (seg, "beats");
        const auto conf = readNum (seg, "confidence");
        const auto chord = chordFrom (seg.getProperty ("chord").toString());
        if (! id || ! start || ! beats || ! conf || ! chord || ! ids.insert (*id).second)
            return std::nullopt;
        if (*start < lastStart || *beats <= 0.0 || *start + *beats > totalBeats + 1.0e-6)
            return std::nullopt;
        lastStart = *start;

        s.id = *id;
        s.startBeat = *start;
        s.beats = *beats;
        s.chord = *chord;
        s.confidence = (float) juce::jlimit (0.0, 1.0, *conf);
        s.userLocked = (int) seg.getProperty ("locked", 0) == 1;
        s.userCorrected = (int) seg.getProperty ("corrected", 0) == 1;

        for (const auto& item : juce::StringArray::fromTokens (seg.getProperty ("alts").toString(), ";", ""))
        {
            if (item.isEmpty())
                continue;
            const auto alt = chordFrom (item.upToFirstOccurrenceOf (":", false, false));
            const auto c = item.fromFirstOccurrenceOf (":", false, false);
            if (! alt || c.isEmpty() || ! c.containsOnly ("0123456789+-.eE") || s.alternatives.size() >= 8)
                return std::nullopt;
            const double cv = c.getDoubleValue();
            if (! std::isfinite (cv))
                return std::nullopt;
            s.alternatives.push_back ({ *alt, (float) juce::jlimit (0.0, 1.0, cv) });
        }
        a.segments.push_back (std::move (s));
    }

    for (auto& m : a.melody)
        if (m.startBeat >= totalBeats)
            return std::nullopt;

    return a;
}

void SessionEngine::persist()
{
    juce::ValueTree t ("Engine");
    if (analysisResult)
    {
        t.setProperty ("source", source, nullptr);
        t.setProperty ("sourceName", sourceName, nullptr);
        t.setProperty ("nextId", nextId, nullptr);
        t.setProperty ("current", currentGenId, nullptr);
        if (cacheHash.isNotEmpty())
            t.setProperty ("audioHash", cacheHash, nullptr);

        const auto current = analysisToTree (*analysisResult);
        t.appendChild (current, nullptr);

        // Generation recipes: the current one plus the most recent others, at most 20 (FR-RH-10).
        juce::ValueTree gens ("Generations");
        std::vector<const Generation*> keep;
        for (auto it = generations.rbegin(); it != generations.rend() && keep.size() < savedGenerations; ++it)
            if (it->id != currentGenId)
                keep.push_back (&*it);
        if (const auto* cur = findGeneration (currentGenId))
        {
            if (keep.size() == savedGenerations)
                keep.pop_back();
            keep.push_back (cur);
        }
        std::sort (keep.begin(), keep.end(), [] (auto* x, auto* y) { return x->id < y->id; });

        for (const auto* g : keep)
        {
            juce::ValueTree gt ("Gen");
            gt.setProperty ("id", g->id, nullptr);
            gt.setProperty ("seed", juce::String ((juce::uint64) g->seed), nullptr);
            gt.setProperty ("locks", idList (g->locks), nullptr);
            gt.appendChild (settingsToTree (g->settings), nullptr);
            // Only the current set needs its exact input; older recipes replay on today's analysis.
            if (g->id == currentGenId && g->input != nullptr)
            {
                auto in = analysisToTree (*g->input);
                if (! sameTree (in, current))
                {
                    juce::ValueTree wrap ("Input");
                    wrap.appendChild (in, nullptr);
                    gt.appendChild (wrap, nullptr);
                }
            }
            gens.appendChild (gt, nullptr);
        }
        t.appendChild (gens, nullptr);

        // The current song as a recipe: request, optional AI profile, idea and chosen options.
        if (song != nullptr)
        {
            for (const auto& r : songs)
            {
                if (r.id != currentSongId)
                    continue;
                juce::ValueTree st ("Song");
                st.setProperty ("id", r.id, nullptr);
                st.setProperty ("request", juce::JSON::toString (r.request, true), nullptr);
                if (r.aiJson.isNotEmpty())
                    st.setProperty ("ai", r.aiJson, nullptr);
                st.setProperty ("bpm", num (r.bpm), nullptr);
                juce::StringArray chosen;
                for (const auto& sec : song->sections)
                    chosen.add (juce::String (sec.chosen));
                st.setProperty ("chosen", chosen.joinIntoString (","), nullptr);
                if (r.idea == nullptr)
                    st.setProperty ("noIdea", 1, nullptr);
                else if (auto idea = analysisToTree (*r.idea); ! sameTree (idea, current))
                {
                    juce::ValueTree wrap ("Idea");
                    wrap.appendChild (idea, nullptr);
                    st.appendChild (wrap, nullptr);
                }
                t.appendChild (st, nullptr);
            }
        }
    }

    const juce::ScopedLock sl (processor.sessionLock);
    processor.session.removeChild (processor.session.getChildWithName ("Engine"), nullptr);
    processor.session.appendChild (t, nullptr);
}

void SessionEngine::restoreState (const juce::ValueTree& t)
{
    clearSession();

    auto parsed = analysisFromTree (t.getChildWithName ("Analysis"));
    if (! parsed)
    {
        if (emit)
            replay();
        return;
    }

    analysisResult = std::move (*parsed);
    const auto currentShared = std::make_shared<const so::analysis::AnalysisResult> (*analysisResult);
    source = t.getProperty ("source", "captured").toString() == "file" ? "file" : "captured";
    sourceName = t.getProperty ("sourceName", {}).toString().substring (0, 512);
    const auto hash = t.getProperty ("audioHash", {}).toString();
    if (hash.length() == 64 && hash.containsOnly ("0123456789abcdef"))
        cacheHash = hash;

    const int savedCurrent = readInt (t, "current", 0, std::numeric_limits<int>::max() - 1).value_or (0);
    int maxId = 0;

    // Generations: recipes in order; the current one is regenerated now (deterministic).
    const auto gens = t.getChildWithName ("Generations");
    for (const auto& gt : gens)
    {
        if (! gt.hasType ("Gen") || generations.size() >= savedGenerations)
            continue;
        const auto id = readInt (gt, "id", 1, std::numeric_limits<int>::max() - 1);
        if (! id || findGeneration (*id) != nullptr)
            continue;
        Generation g;
        g.id = *id;
        g.seed = readU64 (gt, "seed");
        g.locks = idsFrom (gt.getProperty ("locks").toString());
        g.settings = settingsFromTree (gt.getChildWithName ("Settings"));
        g.input = currentShared;
        if (auto in = analysisFromTree (gt.getChildWithName ("Input").getChildWithName ("Analysis")))
            g.input = std::make_shared<const so::analysis::AnalysisResult> (std::move (*in));
        maxId = juce::jmax (maxId, g.id);
        generations.push_back (std::move (g));
    }

    Generation* cur = findGeneration (savedCurrent);
    if (cur == nullptr && ! generations.empty())
        cur = &generations.back();
    if (cur == nullptr)
    {
        Generation g;
        g.id = ++maxId;
        g.settings = currentSettings (0);
        g.input = currentShared;
        generations.push_back (std::move (g));
        cur = &generations.back();
    }
    cur->list = so::reharm::generate (*cur->input, cur->settings, cur->locks);
    suggestionList = cur->list;
    suggestionSeed = cur->seed;
    currentGenId = cur->id;

    // Song: rebuilt from its request with the same seed, then the saved option picks.
    const auto st = t.getChildWithName ("Song");
    if (st.isValid())
    {
        SongRecord rec;
        rec.id = readInt (st, "id", 1, std::numeric_limits<int>::max() - 1).value_or (maxId + 1);
        rec.request = juce::JSON::parse (st.getProperty ("request").toString());
        rec.aiJson = st.getProperty ("ai").toString();
        rec.bpm = juce::jlimit (20.0, 400.0, readNum (st, "bpm").value_or (analysisResult->grid.bpm));
        if ((int) st.getProperty ("noIdea", 0) != 1)
        {
            rec.idea = currentShared;
            if (auto idea = analysisFromTree (st.getChildWithName ("Idea").getChildWithName ("Analysis")))
                rec.idea = std::make_shared<const so::analysis::AnalysisResult> (std::move (*idea));
        }

        if (rec.request.getDynamicObject() != nullptr)
        {
            const auto idea = rec.idea ? std::optional<so::analysis::AnalysisResult> (*rec.idea) : std::nullopt;
            auto built = SongBridge::build (idea, SongBridge::parseRequest (rec.request), rec.aiJson.toStdString(), rec.bpm);
            if (built.song != nullptr)
            {
                const auto chosen = juce::StringArray::fromTokens (st.getProperty ("chosen").toString(), ",", "");
                for (int i = 0; i < juce::jmin (chosen.size(), (int) built.song->sections.size()); ++i)
                {
                    const auto& sec = built.song->sections[(size_t) i];
                    const int pick = chosen[i].getIntValue();
                    if (sec.repeatOf < 0 && pick != sec.chosen && chosen[i].containsOnly ("0123456789"))
                        SongBridge::choose (*built.song, i, pick);
                }
                rec.song = std::move (built.song);
                rec.meta = built.meta;
                maxId = juce::jmax (maxId, rec.id);
                song = rec.song;
                songMeta = rec.meta;
                currentSongId = rec.id;
                songs.push_back (std::move (rec));
            }
        }
    }

    nextId = juce::jmax (maxId + 1, readInt (t, "nextId", 1, std::numeric_limits<int>::max() - 1).value_or (1));
    persist();

    if (emit)
        replay();
}
