#pragma once

// Drag-in audio file loading (FR-IN-02, P1-07). Decodes on a background thread with JUCE's
// AudioFormatManager (WAV, AIFF, FLAC, Ogg, MP3 where the platform build supports it), limits the
// clip to 64 bars or 3 minutes, downmixes to mono and returns an so::analysis::AudioClip.
//
// The message thread only starts jobs, polls progress and receives the result through
// MessageManager::callAsync; it never waits on decoding, so it is never blocked for more than the
// time it takes to queue a job.

#include "core/analysis/AnalysisTypes.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

class AudioFileLoader
{
public:
    struct Result
    {
        bool ok { false };
        bool cancelled { false };
        juce::String error;                 // ASCII, user-facing
        so::analysis::AudioClip clip;
        double sourceSeconds { 0.0 };       // full length of the file
        bool truncated { false };           // longer than the limit, clip holds the first part
    };

    AudioFileLoader();
    ~AudioFileLoader();

    /** 64 bars at `bpm` (if > 0), capped at 3 minutes. */
    static double maxSecondsFor (double bpm, int beatsPerBar = 4);

    /** Message thread. Starts decoding `file` on the worker; cancels any load in flight. `onDone` runs
        on the message thread (not called if this loader is destroyed first). */
    void load (const juce::File& file, double maxSeconds, std::function<void (Result)> onDone);

    /** Cancels the load in flight, if any. Its callback reports cancelled = true. Non-blocking. */
    void cancel();

    bool isLoading() const noexcept { return loading.load(); }
    float progress() const noexcept { return progressValue.load(); }   // 0..1 for the current job

    /** Synchronous decode used by the worker (and tests). Checks `cancelFlag` between chunks and
        writes 0..1 into `progressOut`. Safe to call from any non-audio thread. */
    static Result decodeFile (juce::AudioFormatManager& formats, const juce::File& file, double maxSeconds,
                              const std::atomic<bool>* cancelFlag = nullptr, std::atomic<float>* progressOut = nullptr);

    /** PCM decoded in the WebView (float32 mono at `sampleRate`) to an AudioClip, truncated to
        `maxSeconds`. Non-finite samples become 0. Cheap enough for the message thread. */
    static Result clipFromPcm (const float* samples, std::size_t numSamples, double sampleRate, double maxSeconds);

private:
    class Job;

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("SecondOpinion file decode").withNumberOfThreads (1) };
    std::shared_ptr<std::atomic<bool>> currentCancel;
    std::atomic<bool> loading { false };
    std::atomic<float> progressValue { 0.0f };
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    std::atomic<int> generation { 0 };
};
