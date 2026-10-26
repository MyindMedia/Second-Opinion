#pragma once

#include "AiMoodClient.h"
#include "core/analysis/AnalysisTypes.h"
#include "core/reharm/Settings.h"
#include "core/reharm/Suggestion.h"
#include "plugin/AudioFileLoader.h"
#include "SongBridge.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <set>

class SecondOpinionProcessor;


/** The session's brain on the C++ side (docs/bridge-contract.md). Owned by the processor so the
    analysis, suggestions and song survive closing and reopening the editor.

    Message thread only, except the work it posts to its single worker thread (analysis, reharm,
    song building, PCM decoding). Results come back through MessageManager::callAsync and are
    dropped if a newer job started in the meantime (generation counter) or the engine is gone. */
class SessionEngine
{
public:
    explicit SessionEngine (SecondOpinionProcessor&);
    ~SessionEngine();

    /** Set by the editor; empty while no editor is open. */
    std::function<void (const juce::String& event, const juce::var& payload)> emit;

    // ---- Input ---------------------------------------------------------------------------
    juce::var startCapture();
    juce::var stopCapture();
    juce::var setTempo (double bpm);
    void openAudioFile (std::function<void (juce::var)> complete);
    juce::var analyzePcm (const juce::var& request);
    juce::var cancel();

    // ---- Reading and suggestions ----------------------------------------------------------
    juce::var setChord (const juce::String& segmentId, const juce::String& symbol);
    juce::var lock (const juce::var& segmentIds, bool locked);
    juce::var generate (std::uint64_t seed);

    // ---- Build out ------------------------------------------------------------------------
    juce::var buildSong (const juce::var& request);
    juce::var chooseSectionOption (int sectionIndex, int optionIndex);
    AiMoodClient& ai() { return aiClient; }
    juce::var aiStatus() const;

    // ---- Audition -------------------------------------------------------------------------
    juce::var audition (const juce::var& request);

    /** Editor timer (about 20 Hz): capture and audition state events. */
    void tick();

    /** Re-sends cached analysis, suggestions and song (after the page loads). */
    void replay();

    bool hasAnalysis() const { return analysisResult.has_value(); }

private:
    void analyze (so::analysis::AudioClip clip, so::analysis::BeatGrid grid, juce::String source, juce::String name);
    void emitError (const juce::String& code, const juce::String& message);
    void emitProgress (const juce::String& stage, float pct01);
    void emitCaptureState (const juce::String& state);
    so::reharm::Settings currentSettings (std::uint64_t seed) const;
    std::set<int> lockedIds() const;
    double sessionBpm() const;
    void runSongBuild (const juce::var& request, juce::String aiJson, bool aiFailed);

    SecondOpinionProcessor& processor;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("SecondOpinion engine").withNumberOfThreads (1) };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::shared_ptr<std::atomic<bool>> cancelFlag = std::make_shared<std::atomic<bool>> (false);
    int jobGeneration { 0 };

    AudioFileLoader fileLoader;
    std::unique_ptr<juce::FileChooser> chooser;
    AiMoodClient aiClient;
    juce::String aiLastError;

    // Cached session (message thread)
    std::optional<so::analysis::AnalysisResult> analysisResult;
    juce::String source { "captured" }, sourceName;
    std::vector<so::reharm::Suggestion> suggestionList;
    std::uint64_t suggestionSeed { 0 };
    std::shared_ptr<so::song::Song> song;
    SongBridge::Meta songMeta;

    // Capture bookkeeping
    juce::String lastCaptureState { "idle" };
    std::optional<std::pair<so::analysis::AudioClip, so::analysis::BeatGrid>> pendingClip;   // waiting for a BPM
    bool wasAuditioning { false };
    juce::String auditionTarget, auditionId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionEngine)
};
