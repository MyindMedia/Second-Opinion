#pragma once

#include "Parameters.h"
#include "core/midi/SimpleVoicer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/** Second Opinion's processor. For Phase 1 the audio path is a pure, zero-latency pass-through;
    capture (P1-06) and the preview synth (P3-04) will hook in here.

    Threading (PRD §7.8): processBlock only reads/writes atomics. Session data and the current
    progression live on the message thread. */
class SecondOpinionProcessor final : public juce::AudioProcessor
{
public:
    SecondOpinionProcessor();
    ~SecondOpinionProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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

private:
    static void migrateSession (juce::ValueTree& tree);

    Transport transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecondOpinionProcessor)
};
