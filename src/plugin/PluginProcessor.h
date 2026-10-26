#pragma once

#include "Parameters.h"
#include "core/analysis/AnalysisTypes.h"
#include "core/audio/CaptureMath.h"
#include "core/audio/SpscAudioFifo.h"
#include "core/midi/MidiWriter.h"
#include "core/midi/SimpleVoicer.h"
#include "core/preview/PreviewSynth.h"
#include "core/preview/SnapshotExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

/** Second Opinion's processor. Zero-latency pass-through plus host capture (P1-06) and the
    preview synth (P3-04). See docs/adr/0008-audio-io.md.

    Threading (PRD 7.8): processBlock only reads/writes atomics, pushes into a pre-allocated
    lock-free FIFO and renders the preview synth from an immutable snapshot. Session data and the
    current progression live on the message thread.

    CAPTURE API (FR-IN-01). Message thread unless noted.
      void armCapture();          Idle/Done -> Armed. Recording starts on the next block where the host
                                  transport plays (immediately if it already plays, or if the host has
                                  no playhead). Discards an untaken clip. Ignored while recording.
      void stopCapture();         Armed -> Idle; Recording -> finishing (the drain worker then
                                  produces the clip and the state becomes Done).
      CaptureState captureState() Idle, Armed, Recording (also while the worker finalises), Done.
                                  Any thread.
      float captureProgressBars() Bars recorded so far (at the stamped tempo, or 120 BPM). Any thread.
      float inputLevel()          Decaying input peak, linear 0..1+. Any thread.
      bool needsTempo()           The last capture had no host tempo/PPQ: ask the user for a BPM
                                  (default 120) and rebuild the grid; downbeat = capture start (AC5).
      bool captureOverflowed()    The FIFO overflowed during the last capture (worker starved).
      std::optional<std::pair<AudioClip, BeatGrid>> takeCapturedClip();
                                  Moves the finished clip out (mono downmix, beats = quarter notes,
                                  beat 0 = first host bar line at/after the first sample). Done -> Idle.
      double capturedBar1Ppq()    Host PPQ of grid beat 0 of the last capture.
      void drainCapture();        Moves FIFO -> CaptureBuffer and finalises. Called by the internal
                                  worker thread every 10 ms; exposed for tests. Never the audio thread.

    PREVIEW API (FR-OUT-03, FR-RH-09). Message thread unless noted.
      void setPreviewSnapshot (const std::vector<so::midi::MidiLane>&, double bpm, double lengthBeats);
                                  Builds an immutable snapshot and swaps it in (deferred release).
                                  Lanes whose name contains "bass" use the bass voice.
      void startAudition();       Starts (restarts) auditioning; the output adds the synth.
      void stopAudition();        Fades the synth out over ~50 ms, then output == input exactly.
      bool isAuditioning()        Any thread.
      double auditionBeat()       Current audition position in beats (UI playhead). Any thread.
      void setPreviewSound (PreviewSound);   Keys, Pad or Off (Off = silent, output == input).
      void setAuditionOriginPpq (double);    Host PPQ that maps to beat 0 while the host plays.
                                  Set automatically when a capture completes.
    Audition sync: if the host transport plays, the beat is hostPpq - originPpq (no looping);
    otherwise it free-runs from beat 0 at the snapshot BPM and loops over lengthBeats. */
class SecondOpinionProcessor final : public juce::AudioProcessor
{
public:
    SecondOpinionProcessor();
    ~SecondOpinionProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Host transport, written on the audio thread, read by the editor's timer.
    struct Transport
    {
        std::atomic<bool> playing { false };
        std::atomic<bool> hasTempo { false };
        std::atomic<double> bpm { 120.0 };
        std::atomic<double> ppq { 0.0 };
        std::atomic<int> numerator { 4 };
        std::atomic<int> denominator { 4 };
    };

    const Transport& getTransport() const noexcept { return transport; }

    juce::AudioProcessorValueTreeState apvts;

    /** Session state saved with the project (PRD §7.7). Message thread only. */
    juce::ValueTree session { "Session" };
    static constexpr int stateVersion = 1;

    /** The progression the DragStrip exports (set from the UI via the bridge). Message thread only. */
    struct Progression
    {
        juce::String title;
        juce::String id;
        double bpm { 120.0 };
        std::vector<so::midi::TimedChord> chords;
    };

    Progression progression;
    std::function<void()> onProgressionChanged;

    // ---- Capture (P1-06) ------------------------------------------------------------------
    enum class CaptureState { Idle, Armed, Recording, Done };
    using CapturedClip = std::pair<so::analysis::AudioClip, so::analysis::BeatGrid>;

    void armCapture();
    void stopCapture();
    CaptureState captureState() const noexcept;
    float captureProgressBars() const noexcept;
    float inputLevel() const noexcept { return inputPeak.load (std::memory_order_relaxed); }
    bool needsTempo() const noexcept { return captureNeedsTempo.load(); }
    bool captureOverflowed() const noexcept { return captureOverflow.load(); }
    std::optional<CapturedClip> takeCapturedClip();
    double capturedBar1Ppq() const noexcept { return lastBar1Ppq.load(); }
    void drainCapture();

    // ---- Preview synth (P3-04) -------------------------------------------------------------
    enum class PreviewSound { Keys = 0, Pad = 1, Off = 2 };

    void setPreviewSnapshot (const std::vector<so::midi::MidiLane>& lanes, double bpm, double lengthBeats);
    void startAudition();
    void stopAudition();
    bool isAuditioning() const noexcept { return auditioning.load(); }
    double auditionBeat() const noexcept { return auditionBeatPos.load (std::memory_order_relaxed); }
    void setPreviewSound (PreviewSound s);
    PreviewSound previewSound() const noexcept { return (PreviewSound) previewSoundId.load(); }
    void setAuditionOriginPpq (double ppq) noexcept { auditionOriginPpq.store (ppq); }

private:
    static void migrateSession (juce::ValueTree& tree);

    struct HostPosition
    {
        bool hasPlayHead { false };
        bool playing { false };
        bool hasBpm { false };
        double bpm { 120.0 };
        bool hasPpq { false };
        double ppq { 0.0 };
        bool hasLastBarStart { false };
        double lastBarStart { 0.0 };
        int numerator { 4 };
        int denominator { 4 };
    };

    HostPosition readHost() noexcept;
    void processCapture (const juce::AudioBuffer<float>& buffer, const HostPosition& host) noexcept;
    void processPreview (juce::AudioBuffer<float>& buffer, const HostPosition& host) noexcept;
    void ensureCaptureCapacity (std::size_t frames);   // caller holds captureMutex

    Transport transport;

    // Capture. Internal states extend the public ones with Finishing (audio side done, worker busy).
    enum RawCapture : int { rawIdle = 0, rawArmed, rawRecording, rawFinishing, rawDone };
    std::atomic<int> captureRaw { rawIdle };
    std::atomic<bool> audioInCapture { false };   // audio thread is between state check and push
    std::atomic<std::int64_t> recordedFrames { 0 };
    std::atomic<std::int64_t> limitFrames { 0 };
    std::atomic<std::int64_t> capacityFramesAtomic { 0 };
    std::atomic<bool> captureNeedsTempo { false };
    std::atomic<bool> captureOverflow { false };
    std::atomic<double> lastBar1Ppq { 0.0 };
    std::atomic<float> inputPeak { 0.0f };
    float peakDecayPerBlock { 0.9f };

    // Stamp written by the audio thread before it publishes rawRecording.
    std::atomic<bool> stampHasTempo { false }, stampHasPpq { false }, stampHasLastBar { false };
    std::atomic<double> stampBpm { 120.0 }, stampPpq { 0.0 }, stampLastBar { 0.0 };
    std::atomic<int> stampNum { 4 }, stampDen { 4 };
    std::atomic<std::int64_t> stampSampleTime { 0 };

    std::mutex captureMutex;                       // never taken on the audio thread
    so::audio::SpscAudioFifo captureFifo;
    std::array<std::vector<float>, 2> captureBuffer;
    std::array<std::vector<float>, 2> drainScratch;
    std::size_t captureWritePos { 0 };
    int captureChannels { 2 };
    double currentSampleRate { 48000.0 };
    std::optional<CapturedClip> pendingClip;

    class DrainThread;
    std::unique_ptr<DrainThread> drainThread;

    // Preview
    so::preview::SnapshotExchange<so::preview::PreviewSnapshot> previewExchange;
    so::preview::PreviewSynth synth;
    std::atomic<bool> auditioning { false };
    std::atomic<bool> auditionRestart { false };
    std::atomic<int> previewSoundId { (int) PreviewSound::Keys };
    std::atomic<double> auditionBeatPos { 0.0 };
    std::atomic<double> auditionOriginPpq { 0.0 };
    std::atomic<float>* previewLevelParam { nullptr };
    juce::SmoothedValue<float> previewGain;
    juce::AudioBuffer<float> previewScratch;
    double freeRunBeat { 0.0 };
    int appliedSoundId { -1 };
    bool previewActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecondOpinionProcessor)
};
