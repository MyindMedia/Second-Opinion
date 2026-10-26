// Drag-in file loading (P1-07). Lives in so_rt_tests because it needs JUCE's audio formats.

#include <catch2/catch_test_macros.hpp>

#include "plugin/AudioFileLoader.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace
{
    juce::File writeTestWav (const juce::File& dir, double seconds, double sampleRate = 44100.0)
    {
        auto file = dir.getChildFile ("so_loader_test.wav");
        file.deleteFile();

        juce::WavAudioFormat wav;
        const auto frames = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> data (2, frames);
        for (int i = 0; i < frames; ++i)
        {
            data.setSample (0, i, 0.5f);
            data.setSample (1, i, 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / sampleRate));
        }

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (data, 0, frames));
        writer.reset();
        return file;
    }
}

TEST_CASE ("AudioFileLoader decodes, downmixes and truncates", "[loader]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("so_loader_test");
    dir.createDirectory();
    const auto file = writeTestWav (dir, 1.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::atomic<float> progress { 0.0f };
    auto full = AudioFileLoader::decodeFile (formats, file, 180.0, nullptr, &progress);
    REQUIRE (full.ok);
    CHECK (full.clip.sampleRate == 44100.0);
    CHECK (full.clip.samples.size() == 44100);
    CHECK_FALSE (full.truncated);
    CHECK (progress.load() == 1.0f);
    // Mono = (0.5 + sine * 0.25) / 2, so sample 0 is 0.25 and the mean stays near 0.25.
    CHECK (std::abs (full.clip.samples[0] - 0.25f) < 1.0e-3f);

    auto half = AudioFileLoader::decodeFile (formats, file, 0.5);
    REQUIRE (half.ok);
    CHECK (half.clip.samples.size() == 22050);
    CHECK (half.truncated);
    CHECK (std::abs (half.sourceSeconds - 1.0) < 1e-9);

    std::atomic<bool> cancel { true };
    auto cancelled = AudioFileLoader::decodeFile (formats, file, 180.0, &cancel);
    CHECK_FALSE (cancelled.ok);
    CHECK (cancelled.cancelled);

    auto junk = dir.getChildFile ("junk.wav");
    junk.replaceWithText ("not audio");
    CHECK_FALSE (AudioFileLoader::decodeFile (formats, junk, 180.0).ok);
    CHECK_FALSE (AudioFileLoader::decodeFile (formats, dir.getChildFile ("missing.flac"), 180.0).ok);

    CHECK (AudioFileLoader::maxSecondsFor (120.0) == 128.0);
    CHECK (AudioFileLoader::maxSecondsFor (60.0) == 180.0);
    CHECK (AudioFileLoader::maxSecondsFor (0.0) == 180.0);

    // Async: load() returns at once (the message thread never waits on decoding).
    {
        AudioFileLoader loader;
        const auto t0 = std::chrono::steady_clock::now();
        loader.load (file, 180.0, [] (AudioFileLoader::Result) {});
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        CHECK (ms < 16.0);
        CHECK (loader.isLoading());

        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        while (loader.progress() < 1.0f && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        CHECK (loader.progress() == 1.0f);
    }

    dir.deleteRecursively();
}

TEST_CASE ("AudioFileLoader accepts PCM decoded in the WebView", "[loader]")
{
    std::vector<float> pcm (48000, 0.1f);
    pcm[10] = std::numeric_limits<float>::quiet_NaN();
    pcm[11] = std::numeric_limits<float>::infinity();

    auto r = AudioFileLoader::clipFromPcm (pcm.data(), pcm.size(), 48000.0, 0.5);
    REQUIRE (r.ok);
    CHECK (r.clip.samples.size() == 24000);
    CHECK (r.truncated);
    CHECK (r.clip.samples[10] == 0.0f);
    CHECK (r.clip.samples[11] == 0.0f);
    CHECK (r.clip.samples[12] == 0.1f);

    CHECK_FALSE (AudioFileLoader::clipFromPcm (nullptr, 10, 48000.0, 1.0).ok);
    CHECK_FALSE (AudioFileLoader::clipFromPcm (pcm.data(), pcm.size(), 0.0, 1.0).ok);
}
