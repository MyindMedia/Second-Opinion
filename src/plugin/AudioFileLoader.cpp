#include "AudioFileLoader.h"

#include <cmath>

namespace
{
    constexpr int chunkFrames = 32768;
}

class AudioFileLoader::Job final : public juce::ThreadPoolJob
{
public:
    Job (AudioFileLoader& o, juce::File f, double maxSec, std::shared_ptr<std::atomic<bool>> cancelFlag,
         int gen, std::function<void (Result)> done)
        : juce::ThreadPoolJob ("decode"), owner (o), file (std::move (f)), maxSeconds (maxSec),
          cancel (std::move (cancelFlag)), generation (gen), onDone (std::move (done)), alive (o.alive)
    {
    }

    JobStatus runJob() override
    {
        auto result = decodeFile (owner.formats, file, maxSeconds, cancel.get(), &owner.progressValue);
        if (shouldExit())
            result = Result { false, true, "Cancelled", {}, 0.0, false };

        auto* loaderPtr = &owner;
        juce::MessageManager::callAsync (
            [loaderPtr, aliveFlag = alive, gen = generation, cb = std::move (onDone), r = std::move (result)]() mutable
            {
                if (! aliveFlag->load())
                    return;
                if (loaderPtr->generation.load() == gen)
                    loaderPtr->loading.store (false);
                if (cb)
                    cb (std::move (r));
            });
        return jobHasFinished;
    }

private:
    AudioFileLoader& owner;
    juce::File file;
    double maxSeconds;
    std::shared_ptr<std::atomic<bool>> cancel;
    int generation;
    std::function<void (Result)> onDone;
    std::shared_ptr<std::atomic<bool>> alive;
};

AudioFileLoader::AudioFileLoader()
{
    formats.registerBasicFormats();
}

AudioFileLoader::~AudioFileLoader()
{
    alive->store (false);
    if (currentCancel != nullptr)
        currentCancel->store (true);
    pool.removeAllJobs (true, 5000);
}

double AudioFileLoader::maxSecondsFor (double bpm, int beatsPerBar)
{
    double seconds = 180.0;
    if (bpm > 0.0)
        seconds = juce::jmin (seconds, 64.0 * (double) juce::jmax (1, beatsPerBar) * 60.0 / bpm);
    return seconds;
}

void AudioFileLoader::load (const juce::File& file, double maxSeconds, std::function<void (Result)> onDone)
{
    cancel();
    currentCancel = std::make_shared<std::atomic<bool>> (false);
    progressValue.store (0.0f);
    loading.store (true);
    const int gen = ++generation;
    pool.addJob (new Job (*this, file, maxSeconds, currentCancel, gen, std::move (onDone)), true);
}

void AudioFileLoader::cancel()
{
    if (currentCancel != nullptr)
        currentCancel->store (true);
}

AudioFileLoader::Result AudioFileLoader::decodeFile (juce::AudioFormatManager& formats, const juce::File& file,
                                                     double maxSeconds, const std::atomic<bool>* cancelFlag,
                                                     std::atomic<float>* progressOut)
{
    Result r;
    auto setProgress = [progressOut] (float p) { if (progressOut != nullptr) progressOut->store (p); };
    auto cancelled = [cancelFlag] { return cancelFlag != nullptr && cancelFlag->load(); };

    if (! file.existsAsFile())
    {
        r.error = "File not found";
        return r;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        r.error = "Unsupported or unreadable audio file";
        return r;
    }

    const double sr = reader->sampleRate;
    const int channels = (int) reader->numChannels;
    if (! (sr > 0.0) || channels <= 0 || reader->lengthInSamples <= 0)
    {
        r.error = "The file contains no audio";
        return r;
    }

    r.sourceSeconds = (double) reader->lengthInSamples / sr;
    const auto limitFrames = (juce::int64) std::floor (juce::jmax (0.0, maxSeconds) * sr);
    const auto total = juce::jmin (reader->lengthInSamples, limitFrames);
    r.truncated = reader->lengthInSamples > limitFrames;

    r.clip.sampleRate = sr;
    r.clip.samples.assign ((std::size_t) total, 0.0f);

    juce::AudioBuffer<float> chunk (channels, chunkFrames);
    const float scale = 1.0f / (float) channels;

    for (juce::int64 pos = 0; pos < total; pos += chunkFrames)
    {
        if (cancelled())
        {
            r = Result {};
            r.cancelled = true;
            r.error = "Cancelled";
            return r;
        }

        const int n = (int) juce::jmin ((juce::int64) chunkFrames, total - pos);
        if (! reader->read (&chunk, 0, n, pos, true, true))
        {
            r = Result {};
            r.error = "Decoding failed";
            return r;
        }

        float* dst = r.clip.samples.data() + pos;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = chunk.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                dst[i] += std::isfinite (src[i]) ? src[i] * scale : 0.0f;
        }

        setProgress ((float) ((double) (pos + n) / (double) total));
    }

    setProgress (1.0f);
    r.ok = true;
    return r;
}

AudioFileLoader::Result AudioFileLoader::clipFromPcm (const float* samples, std::size_t numSamples, double sampleRate,
                                                      double maxSeconds)
{
    Result r;
    if (samples == nullptr || numSamples == 0 || ! (sampleRate > 0.0) || ! std::isfinite (sampleRate))
    {
        r.error = "No audio";
        return r;
    }

    r.sourceSeconds = (double) numSamples / sampleRate;
    const auto limit = (std::size_t) std::floor (juce::jmax (0.0, maxSeconds) * sampleRate);
    const auto n = juce::jmin (numSamples, limit);
    r.truncated = numSamples > limit;

    r.clip.sampleRate = sampleRate;
    r.clip.samples.resize (n);
    for (std::size_t i = 0; i < n; ++i)
        r.clip.samples[i] = std::isfinite (samples[i]) ? samples[i] : 0.0f;
    r.ok = n > 0;
    if (! r.ok)
        r.error = "No audio";
    return r;
}
