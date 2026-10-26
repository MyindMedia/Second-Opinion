// Real-time safety test (PRD §10, NFR-05): runs processBlock while a global allocation trap is armed.
// Any heap allocation on the "audio thread" during the armed window fails the test.

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "plugin/PluginProcessor.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
    std::atomic<bool> trapArmed { false };
    std::atomic<int> trappedAllocations { 0 };

    void* tracked (std::size_t n)
    {
        if (trapArmed.load (std::memory_order_relaxed))
            trappedAllocations.fetch_add (1, std::memory_order_relaxed);
        if (void* p = std::malloc (n == 0 ? 1 : n))
            return p;
        throw std::bad_alloc();
    }

    struct ScopedTrap
    {
        ScopedTrap()  { trappedAllocations = 0; trapArmed = true; }
        ~ScopedTrap() { trapArmed = false; }
    };

    class FakePlayHead final : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setBpm (92.0);
            info.setPpqPosition (ppq);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
        double ppq = 0.0;
    };
}

void* operator new (std::size_t n) { return tracked (n); }
void* operator new[] (std::size_t n) { return tracked (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

TEST_CASE ("The allocation trap detects allocations (negative control)", "[rt]")
{
    {
        ScopedTrap trap;
        auto* leak = new std::vector<int> (16);
        delete leak;
    }
    CHECK (trappedAllocations.load() >= 2);
}

TEST_CASE ("processBlock is allocation-free and bit-identical", "[rt]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor proc;
    FakePlayHead playHead;
    proc.setPlayHead (&playHead);
    proc.setPlayConfigDetails (2, 2, 48000.0, 128);
    proc.prepareToPlay (48000.0, 128);

    juce::AudioBuffer<float> buffer (2, 128);
    juce::AudioBuffer<float> reference (2, 128);
    juce::MidiBuffer midi;
    juce::Random rng (1234); // explicit seed (determinism rule)

    for (int block = 0; block < 2000; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
                buffer.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

        reference.makeCopyOf (buffer, true);
        playHead.ppq = block * 128.0 / 48000.0 * (92.0 / 60.0);

        {
            ScopedTrap trap;
            proc.processBlock (buffer, midi);
        }

        REQUIRE (trappedAllocations.load() == 0);

        for (int ch = 0; ch < 2; ++ch)
            REQUIRE (std::memcmp (buffer.getReadPointer (ch), reference.getReadPointer (ch), 128 * sizeof (float)) == 0);
    }

    CHECK (proc.getLatencySamples() == 0);
    CHECK (proc.getTransport().bpm.load() == 92.0);
    proc.setPlayHead (nullptr);
}

TEST_CASE ("State round-trips and ignores garbage", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor a;
    a.apvts.getParameter (so::params::tension)->setValueNotifyingHost (0.8f);
    a.session.setProperty ("uiWidth", 1234, nullptr);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    SecondOpinionProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    CHECK (b.apvts.getRawParameterValue (so::params::tension)->load() == 80.0f);
    CHECK ((int) b.session.getProperty ("uiWidth") == 1234);
    CHECK ((int) b.session.getProperty ("stateVersion") == SecondOpinionProcessor::stateVersion);

    // Garbage must be ignored without crashing or changing state.
    juce::Random rng (42);
    for (int i = 0; i < 200; ++i)
    {
        juce::MemoryBlock junk ((size_t) rng.nextInt ({ 1, 512 }));
        rng.fillBitsRandomly (junk.getData(), junk.getSize());
        b.setStateInformation (junk.getData(), (int) junk.getSize());
    }
    b.setStateInformation (nullptr, 0);
    CHECK (b.apvts.getRawParameterValue (so::params::tension)->load() == 80.0f);
}

int main (int argc, char* argv[])
{
    return Catch::Session().run (argc, argv);
}
