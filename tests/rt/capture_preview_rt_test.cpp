// Capture (P1-06) and preview synth (P3-04) through the real processor, with processBlock under the
// allocation trap: zero allocations, bit-identical pass-through, bar alignment within one sample.

#include <catch2/catch_test_macros.hpp>

#include "RtTestSupport.h"
#include "plugin/PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

using rt::FakePlayHead;
using rt::ScopedTrap;
using rt::trappedAllocations;
using State = SecondOpinionProcessor::CaptureState;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;

    bool waitForState (SecondOpinionProcessor& p, State s, int timeoutMs = 3000)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
        while (p.captureState() != s && std::chrono::steady_clock::now() < until)
        {
            p.drainCapture();
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        return p.captureState() == s;
    }

    bool sameAudio (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), (size_t) a.getNumSamples() * sizeof (float)) != 0)
                return false;
        return true;
    }

    std::vector<so::midi::MidiLane> lanes()
    {
        so::midi::MidiLane chords { "Chords", {} }, bass { "Bass", {} };
        for (int bar = 0; bar < 4; ++bar)
        {
            for (int k : { 0, 4, 7 })
                chords.notes.push_back ({ 60 + k + bar, bar * 4.0, 4.0, 100, 0 });
            bass.notes.push_back ({ 36 + bar, bar * 4.0, 4.0, 100, 1 });
        }
        return { chords, bass };
    }
}

TEST_CASE ("Capture: allocation-free, bit-identical, bar-aligned within one sample", "[rt][capture]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor proc;
    FakePlayHead host;
    host.playing = false;
    host.bpm = 92.0;
    proc.setPlayHead (&host);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buffer (2, block), reference (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (777);

    proc.armCapture();
    REQUIRE (proc.captureState() == State::Armed);

    // Transport stopped: armed, nothing recorded.
    for (int b = 0; b < 10; ++b)
    {
        buffer.clear();
        ScopedTrap trap;
        proc.processBlock (buffer, midi);
    }
    REQUIRE (proc.captureState() == State::Armed);

    // Transport starts mid-bar at PPQ 10.3 (bar 3 spans 8..12). The next bar line (PPQ 12) is marked
    // with an impulse so we can find it in the captured clip.
    const double ppq0 = 10.3;
    const double qnPerSample = host.bpm / 60.0 / sr;
    const auto barLineSample = (int) std::ceil ((12.0 - ppq0) / qnPerSample);
    host.playing = true;

    std::vector<float> expectedMono;
    const int blocks = 2000;   // ~5.3 s, about 2 bars at 92 BPM
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.1f);
        const int local = barLineSample - b * block;
        if (local >= 0 && local < block)
        {
            buffer.setSample (0, local, 1.0f);
            buffer.setSample (1, local, 1.0f);
        }

        reference.makeCopyOf (buffer, true);
        for (int i = 0; i < block; ++i)
            expectedMono.push_back (0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)));

        host.ppq = ppq0 + (double) (b * block) * qnPerSample;
        host.timeInSamples = b * block;
        {
            ScopedTrap trap;
            proc.processBlock (buffer, midi);
        }
        REQUIRE (trappedAllocations.load() == 0);
        REQUIRE (sameAudio (buffer, reference));

        if (b % 8 == 0)
            proc.drainCapture();   // the worker thread drains too; this just keeps the FIFO short
    }

    CHECK (proc.captureState() == State::Recording);
    CHECK (proc.captureProgressBars() > 1.9f);

    // Transport stops: capture finishes on the next block.
    host.playing = false;
    {
        ScopedTrap trap;
        proc.processBlock (buffer, midi);
    }
    REQUIRE (trappedAllocations.load() == 0);
    REQUIRE (waitForState (proc, State::Done));
    CHECK_FALSE (proc.needsTempo());
    CHECK_FALSE (proc.captureOverflowed());

    auto taken = proc.takeCapturedClip();
    REQUIRE (taken.has_value());
    CHECK (proc.captureState() == State::Idle);

    const auto& [clip, grid] = *taken;
    REQUIRE (clip.samples.size() == expectedMono.size());
    CHECK (clip.samples == expectedMono);
    CHECK (clip.sampleRate == sr);
    CHECK (grid.bpm == 92.0);
    CHECK (grid.beatsPerBar == 4);

    // The impulse sits at the DAW bar line; the grid's beat 0 must land on it within +-1 sample.
    std::size_t impulseAt = 0;
    for (std::size_t i = 0; i < clip.samples.size(); ++i)
        if (clip.samples[i] > 0.9f) { impulseAt = i; break; }
    REQUIRE (impulseAt == (std::size_t) barLineSample);
    CHECK (std::abs (grid.beatToSec (0.0) * sr - (double) impulseAt) <= 1.0);
    CHECK (std::abs (proc.capturedBar1Ppq() - 12.0) < 1e-9);
    proc.setPlayHead (nullptr);
}

TEST_CASE ("Capture stops at the 64-bar limit", "[rt][capture]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor proc;
    FakePlayHead host;
    host.bpm = 300.0;      // 64 bars = 51.2 s, keeps the test short
    host.playing = false;
    proc.setPlayHead (&host);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    buffer.clear();
    proc.processBlock (buffer, midi);   // the processor learns the tempo
    proc.armCapture();                  // and sizes the buffer for it
    host.playing = true;

    const auto limit = (std::size_t) (64.0 * 4.0 * 60.0 / 300.0 * sr);
    int blocks = 0;
    while (proc.captureState() != State::Done && blocks < 30000)
    {
        host.ppq = (double) (blocks * block) * 300.0 / 60.0 / sr;
        {
            ScopedTrap trap;
            proc.processBlock (buffer, midi);
        }
        REQUIRE (trappedAllocations.load() == 0);
        if (++blocks % 16 == 0)
            proc.drainCapture();
    }
    REQUIRE (waitForState (proc, State::Done));
    auto taken = proc.takeCapturedClip();
    REQUIRE (taken.has_value());
    CHECK (taken->first.samples.size() == limit);
    proc.setPlayHead (nullptr);
}

TEST_CASE ("Capture without a playhead starts at once and asks for a tempo", "[rt][capture]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    buffer.clear();

    proc.armCapture();
    for (int b = 0; b < 100; ++b)
    {
        ScopedTrap trap;
        proc.processBlock (buffer, midi);
    }
    REQUIRE (trappedAllocations.load() == 0);
    CHECK (proc.captureState() == State::Recording);
    CHECK (proc.needsTempo());

    proc.stopCapture();
    REQUIRE (waitForState (proc, State::Done));
    auto taken = proc.takeCapturedClip();
    REQUIRE (taken.has_value());
    CHECK (taken->first.samples.size() == (std::size_t) (100 * block));
    CHECK (taken->second.downbeatSec == 0.0);
    CHECK (taken->second.bpm == 120.0);

    // Disarming before anything is recorded returns to Idle.
    proc.armCapture();
    proc.stopCapture();
    CHECK (proc.captureState() == State::Idle);
}

TEST_CASE ("Preview: allocation-free, audible while auditioning, exact pass-through otherwise", "[rt][preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor proc;
    FakePlayHead host;
    host.playing = false;   // free-run audition
    proc.setPlayHead (&host);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    proc.setPreviewSnapshot (lanes(), 120.0, 16.0);

    juce::AudioBuffer<float> buffer (2, block), reference (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (99);

    auto runBlocks = [&] (int count, bool expectIdentical)
    {
        bool anyDifferent = false;
        for (int b = 0; b < count; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.25f);
            reference.makeCopyOf (buffer, true);
            {
                ScopedTrap trap;
                proc.processBlock (buffer, midi);
            }
            REQUIRE (trappedAllocations.load() == 0);
            const bool same = sameAudio (buffer, reference);
            if (expectIdentical)
                REQUIRE (same);
            anyDifferent = anyDifferent || ! same;
        }
        return anyDifferent;
    };

    runBlocks (50, true);                         // not auditioning: output == input

    proc.startAudition();
    CHECK (proc.isAuditioning());
    CHECK (runBlocks (400, false));               // ~1 s: the synth is mixed in
    CHECK (proc.auditionBeat() > 1.5);

    // Swap the snapshot mid-audition (message thread), then switch to pad: still allocation-free.
    proc.setPreviewSnapshot (lanes(), 100.0, 8.0);
    proc.setPreviewSound (SecondOpinionProcessor::PreviewSound::Pad);
    CHECK (runBlocks (400, false));

    // Free-run loops over lengthBeats.
    for (int b = 0; b < 2000; ++b)
        runBlocks (1, false);
    CHECK (proc.auditionBeat() < 8.0);

    proc.stopAudition();
    runBlocks (40, false);                        // ~100 ms fade-out
    runBlocks (200, true);                        // then bit-identical again

    // Following the host: beat = PPQ - origin (the capture's bar 1).
    proc.setAuditionOriginPpq (8.0);
    host.playing = true;
    host.bpm = 120.0;
    host.ppq = 9.0;
    proc.startAudition();
    runBlocks (1, false);
    CHECK (std::abs (proc.auditionBeat() - 1.0) < 1e-9);
    proc.stopAudition();
    proc.setPlayHead (nullptr);
}

TEST_CASE ("Preview: processBlock CPU while auditioning (48 kHz / 128)", "[rt][preview][bench]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SecondOpinionProcessor proc;
    FakePlayHead host;
    host.playing = false;
    proc.setPlayHead (&host);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    // 12 chord voices + 4 bass voices held throughout, the busiest realistic case.
    so::midi::MidiLane chords { "Chords", {} }, bass { "Bass", {} };
    for (int i = 0; i < 12; ++i) chords.notes.push_back ({ 55 + i * 2, 0.0, 400.0, 100, 0 });
    for (int i = 0; i < 4; ++i) bass.notes.push_back ({ 33 + i * 7, 0.0, 400.0, 100, 1 });
    proc.setPreviewSnapshot ({ chords, bass }, 120.0, 400.0);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (10.0 * sr / block);

    for (auto sound : { SecondOpinionProcessor::PreviewSound::Keys, SecondOpinionProcessor::PreviewSound::Pad })
    {
        proc.setPreviewSound (sound);
        proc.startAudition();
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("[bench] processBlock auditioning %s: %.2f%% of one core\n",
                     sound == SecondOpinionProcessor::PreviewSound::Keys ? "keys" : "pad", 100.0 * secs / 10.0);
        proc.stopAudition();
    }

    // Idle / capture path for NFR-04's "< 1%" figure.
    proc.armCapture();
    host.playing = true;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 2000; ++b)
    {
        host.ppq = b * block * 2.0 / sr;
        proc.processBlock (buffer, midi);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("[bench] processBlock capturing: %.3f%% of one core\n", 100.0 * secs / (2000.0 * block / sr));
    proc.stopCapture();
    proc.setPlayHead (nullptr);
    SUCCEED();
}
