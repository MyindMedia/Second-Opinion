#pragma once

#include "AiMoodClient.h"
#include "core/analysis/Analyzer.h"
#include "core/analysis/AnalysisTypes.h"
#include "core/reharm/Settings.h"
#include "core/reharm/Suggestion.h"
#include "plugin/AudioFileLoader.h"
#include "SongBridge.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <deque>
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

    /** Re-sends cached analysis, suggestions and song (after the page loads). Suggestions carry the
        selected id and the song is flagged `restored` so the UI shows the exact saved view. */
    void replay();

    bool hasAnalysis() const { return analysisResult.has_value(); }

    // ---- Settings (FR-SET, setSettings) ----------------------------------------------------
    struct UserSettings
    {
        so::analysis::AnalysisQuality quality { so::analysis::AnalysisQuality::Balanced };
        so::analysis::SourceHint source { so::analysis::SourceHint::Auto };
        int ppq { 960 };            // 480 or 960
        bool humanize { true };     // velocity: humanized (seeded) or flat
    };

    /** Applies the UI Settings object (ui/src/state/store.ts) and stores it as session ui_settings. */
    juce::var setSettings (const juce::var& settings);
    /** Re-reads ui_settings from the session (after setStateInformation). */
    void loadSettingsFromSession();
    const UserSettings& userSettings() const noexcept { return prefs; }
    so::analysis::AnalyzerOptions analyzerOptions() const;

    /** Deletes the captured-audio cache (FR-SET "Clear captured audio"). */
    juce::var clearCache();
    juce::String cachedAudioHash() const { return cacheHash; }

    // ---- Undo / redo (FR-ST-02) -------------------------------------------------------------
    /** Puts the native session back to a UI undo snapshot: segment chords and locks, which
        generation of suggestions is current, and which song (or none) with which options. */
    juce::var restoreSnapshot (const juce::var& snapshot);

    // ---- Save and recall (FR-ST-01) ---------------------------------------------------------
    /** Writes the engine state into the processor's session tree (child "Engine"). Called after
        every change, so getStateInformation only copies the session. Message thread. */
    void persist();
    /** Restores from an "Engine" tree: analysis as saved, suggestions and song rebuilt
        deterministically from their recipes (no analysis is re-run). Invalid data is dropped.
        Message thread; replays to an open editor. */
    void restoreState (const juce::ValueTree& engineTree);

    static juce::ValueTree analysisToTree (const so::analysis::AnalysisResult&);
    static std::optional<so::analysis::AnalysisResult> analysisFromTree (const juce::ValueTree&);

    // ---- Synchronous entry points (restore and tests) ---------------------------------------
    /** Takes a finished analysis as if it had just been computed: generates the seed-0 suggestions. */
    void adoptAnalysis (so::analysis::AnalysisResult result, const juce::String& source, const juce::String& name);
    /** generate() on the calling thread (no event). */
    bool generateNow (std::uint64_t seed);
    /** Builds the song on the calling thread (offline mood engine, or aiJson when given). */
    bool buildSongNow (const juce::var& request, const juce::String& aiJson = {});

    const std::optional<so::analysis::AnalysisResult>& analysis() const noexcept { return analysisResult; }
    const std::vector<so::reharm::Suggestion>& suggestions() const noexcept { return suggestionList; }
    const so::song::Song* currentSong() const noexcept { return song.get(); }
    const SongBridge::Meta& currentSongMeta() const noexcept { return songMeta; }
    std::uint64_t currentSeed() const noexcept { return suggestionSeed; }
    int currentGeneration() const noexcept { return currentGenId; }
    int currentSongGeneration() const noexcept { return currentSongId; }
    std::vector<std::uint64_t> seedHistory() const;

private:
    void analyze (so::analysis::AudioClip clip, so::analysis::BeatGrid grid, juce::String source, juce::String name);
    void emitError (const juce::String& code, const juce::String& message);
    void emitProgress (const juce::String& stage, float pct01);
    void emitCaptureState (const juce::String& state);
    so::reharm::Settings currentSettings (std::uint64_t seed) const;
    std::set<int> lockedIds() const;
    double sessionBpm() const;
    std::optional<double> hostTempo() const;
    void runSongBuild (const juce::var& request, juce::String aiJson, bool aiFailed);

    /** One generated set and everything needed to make it again. */
    struct Generation
    {
        int id { 0 };
        std::uint64_t seed { 0 };                   // as sent by the UI (settings.seed is seed + 1)
        so::reharm::Settings settings;
        std::set<int> locks;
        std::shared_ptr<const so::analysis::AnalysisResult> input;
        std::vector<so::reharm::Suggestion> list;   // empty for recipes loaded from a project
    };

    /** One built song and its recipe. */
    struct SongRecord
    {
        int id { 0 };
        juce::var request;                          // BuildSongRequest as received
        juce::String aiJson;                        // online MoodProfile, empty = offline
        double bpm { 120.0 };
        std::shared_ptr<const so::analysis::AnalysisResult> idea;   // null = built without audio
        std::shared_ptr<so::song::Song> song;
        SongBridge::Meta meta;
    };

    void pushGeneration (Generation g);
    Generation* findGeneration (int id);
    void clearSession();
    void adoptSong (SongRecord rec);
    juce::var suggestionsPayload() const;
    juce::var songPayload (bool restored) const;
    void startCacheWrite (std::shared_ptr<const so::analysis::AudioClip> clip);

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

    // History (in memory, for undo; the last 20 generation recipes are saved with the project)
    std::deque<Generation> generations;             // back() is current
    std::deque<SongRecord> songs;                   // back() is current when `song` is set
    int nextId { 1 };
    int currentGenId { 0 }, currentSongId { 0 };
    static constexpr size_t maxHistory = 64;        // >= 50 undo steps (FR-ST-02)
    static constexpr size_t savedGenerations = 20;  // FR-RH-10

    UserSettings prefs;
    juce::String cacheHash;
    int analysisSerial { 0 };
    juce::ThreadPool ioPool { juce::ThreadPoolOptions{}.withThreadName ("SecondOpinion cache").withNumberOfThreads (1) };

    // Capture bookkeeping
    juce::String lastCaptureState { "idle" };
    std::optional<std::pair<so::analysis::AudioClip, so::analysis::BeatGrid>> pendingClip;   // waiting for a BPM
    bool wasAuditioning { false };
    juce::String auditionTarget, auditionId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionEngine)
};
