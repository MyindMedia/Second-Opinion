#pragma once

// Shared pieces of the real-time safety harness (PRD 10, NFR-05). The global operator new/delete
// replacements live in realtime_safety_test.cpp; they count allocations made on the thread that
// armed the trap, so background workers (the capture drain, file decoding) cannot cause false hits.

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace rt
{
    extern thread_local bool trapArmedOnThisThread;
    extern std::atomic<int> trappedAllocations;

    struct ScopedTrap
    {
        ScopedTrap()  { trappedAllocations = 0; trapArmedOnThisThread = true; }
        ~ScopedTrap() { trapArmedOnThisThread = false; }
        ScopedTrap (const ScopedTrap&) = delete;
        ScopedTrap& operator= (const ScopedTrap&) = delete;
    };

    /** A host transport the test drives by hand. */
    class FakePlayHead final : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override
        {
            if (! hasPosition)
                return {};
            PositionInfo info;
            info.setIsPlaying (playing);
            if (hasTempo)
                info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setTimeSignature (TimeSignature { numerator, denominator });
            info.setTimeInSamples (timeInSamples);
            return info;
        }

        bool hasPosition = true;
        bool playing = true;
        bool hasTempo = true;
        double bpm = 92.0;
        double ppq = 0.0;
        int numerator = 4;
        int denominator = 4;
        juce::int64 timeInSamples = 0;
    };
}
