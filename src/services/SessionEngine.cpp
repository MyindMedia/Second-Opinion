#include "SessionEngine.h"

#include "bridge/Json.h"
#include "core/analysis/Analyzer.h"
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

    auto post = [this, aliveFlag, gen] (std::function<void()> fn)
    {
        juce::MessageManager::callAsync ([this, aliveFlag, gen, fn = std::move (fn)]
        {
            if (aliveFlag->load() && gen == jobGeneration)
                fn();
        });
    };

    pool.addJob ([this, clip = std::move (clip), grid, src, name, cancelPtr, settings, post]
    {
        so::analysis::AnalyzerOptions options;
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

        post ([this, result = std::move (result), list = std::move (list), src, name]() mutable
        {
            analysisResult = std::move (result);
            source = src;
            sourceName = name;
            suggestionList = std::move (list);
            suggestionSeed = 0;
            song.reset();
            emitProgress ("suggestions", 1.0f);
            if (emit)
            {
                emit ("analysisReady", json::analysis (*analysisResult, source, sourceName));
                emit ("suggestionsReady", json::suggestions (suggestionList, suggestionSeed));
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
            so::analysis::BeatGrid grid;
            grid.bpm = bpm;
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
            so::analysis::BeatGrid grid;
            grid.bpm = bpm;
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
    auto analysisCopy = *analysisResult;
    auto settings = currentSettings (seed);
    auto locked = lockedIds();

    pool.addJob ([this, aliveFlag, gen, analysisCopy = std::move (analysisCopy), settings, locked, seed]
    {
        auto list = so::reharm::generate (analysisCopy, settings, locked);
        juce::MessageManager::callAsync ([this, aliveFlag, gen, list = std::move (list), seed]() mutable
        {
            if (! aliveFlag->load() || gen != jobGeneration)
                return;
            suggestionList = std::move (list);
            suggestionSeed = seed;
            if (emit)
                emit ("suggestionsReady", json::suggestions (suggestionList, suggestionSeed));
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

    pool.addJob ([this, aliveFlag, gen, analysisCopy = std::move (analysisCopy), req, aiJson, bpm]
    {
        auto built = SongBridge::build (analysisCopy, req, aiJson.toStdString(), bpm);

        juce::MessageManager::callAsync ([this, aliveFlag, gen, built = std::move (built)]() mutable
        {
            if (! aliveFlag->load() || gen != jobGeneration)
                return;
            if (built.song == nullptr)
            {
                emitError ("song_failed", built.error.isNotEmpty() ? built.error : juce::String ("The song couldn't be built. Try again."));
                return;
            }
            song = std::move (built.song);
            songMeta = built.meta;
            emitProgress ("song", 1.0f);
            if (emit)
                emit ("songReady", SongBridge::toJson (*song, songMeta));
        });
    });
}

var SessionEngine::chooseSectionOption (int sectionIndex, int optionIndex)
{
    if (song == nullptr || ! SongBridge::choose (*song, sectionIndex, optionIndex))
        return json::ack (false);
    return json::ack (true);
}

void SessionEngine::replay()
{
    if (! emit)
        return;
    if (analysisResult)
    {
        emit ("analysisReady", json::analysis (*analysisResult, source, sourceName));
        emit ("suggestionsReady", json::suggestions (suggestionList, suggestionSeed));
    }
    if (song != nullptr)
        emit ("songReady", SongBridge::toJson (*song, songMeta));
}
