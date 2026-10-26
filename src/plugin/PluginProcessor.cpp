#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "services/SessionEngine.h"

#include <cmath>
#include <thread>

// Drains the capture FIFO every 10 ms while capturing (50 ms when idle). Never the audio thread.
class SecondOpinionProcessor::DrainThread final : public juce::Thread
{
public:
    explicit DrainThread (SecondOpinionProcessor& p) : juce::Thread ("SecondOpinion capture drain"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            owner.drainCapture();
            const int s = owner.captureRaw.load();
            wait (s == rawIdle || s == rawDone ? 50 : 10);
        }
    }

private:
    SecondOpinionProcessor& owner;
};

SecondOpinionProcessor::SecondOpinionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", so::params::createLayout())
{
    session.setProperty ("stateVersion", stateVersion, nullptr);
    previewLevelParam = apvts.getRawParameterValue (so::params::previewLevel);

    drainThread = std::make_unique<DrainThread> (*this);
    drainThread->startThread();

    engine = std::make_unique<SessionEngine> (*this);
}

SecondOpinionProcessor::~SecondOpinionProcessor()
{
    engine.reset();   // stops engine jobs before the processor goes away

    if (drainThread != nullptr)
        drainThread->stopThread (2000);
}

void SecondOpinionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    setLatencySamples (0); // NFR-03: all analysis is offline, the audio path adds nothing.

    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int block = juce::jmax (samplesPerBlock, 32);
    peakDecayPerBlock = (float) std::exp (-(double) block / (0.3 * currentSampleRate));

    {
        std::lock_guard<std::mutex> lock (captureMutex);

        // A stream restart aborts a capture in flight; a finished, untaken clip survives.
        const int s = captureRaw.load();
        if (s == rawRecording || s == rawFinishing || s == rawArmed)
            captureRaw.store (rawIdle);

        captureChannels = juce::jlimit (1, 2, getTotalNumInputChannels());
        const auto fifoFrames = (std::size_t) juce::jmax ((double) block * 4.0, std::ceil (currentSampleRate * 0.5));
        captureFifo.reset (captureChannels, fifoFrames);
        for (auto& v : drainScratch)
            v.assign (fifoFrames, 0.0f);

        const double bpm = transport.hasTempo.load() ? transport.bpm.load() : 120.0;
        const auto frames = so::audio::captureCapacityFrames (currentSampleRate, bpm, transport.numerator.load(),
                                                              transport.denominator.load());
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& buf = captureBuffer[(std::size_t) ch];
            if (ch >= captureChannels)
                std::vector<float>().swap (buf);
            else if (buf.size() != frames)
                buf.assign (frames, 0.0f);
        }
        capacityFramesAtomic.store ((std::int64_t) frames);
        captureWritePos = 0;
    }

    synth.prepare (currentSampleRate);
    previewScratch.setSize (2, block, false, true, false);
    previewGain.reset (currentSampleRate, 0.05);
    previewGain.setCurrentAndTargetValue (0.0f);
    previewActive = false;
    appliedSoundId = -1;
    freeRunBeat = 0.0;
}

void SecondOpinionProcessor::releaseResources()
{
    previewActive = false;
}

bool SecondOpinionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

SecondOpinionProcessor::HostPosition SecondOpinionProcessor::readHost() noexcept
{
    HostPosition h;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            h.hasPlayHead = true;
            h.playing = pos->getIsPlaying();

            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0)
            {
                h.hasBpm = true;
                h.bpm = *bpm;
            }
            if (const auto ppq = pos->getPpqPosition())
            {
                h.hasPpq = true;
                h.ppq = *ppq;
            }
            if (const auto bar = pos->getPpqPositionOfLastBarStart())
            {
                h.hasLastBarStart = true;
                h.lastBarStart = *bar;
            }
            if (const auto ts = pos->getTimeSignature())
            {
                h.numerator = ts->numerator;
                h.denominator = ts->denominator;
            }
        }
    }
    return h;
}

void SecondOpinionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Real-time safe: no allocation, no locks, no I/O. Audio passes through untouched (bit-identical)
    // unless the preview synth is auditioning.
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const auto host = readHost();
    if (host.hasPlayHead)
    {
        transport.playing.store (host.playing, std::memory_order_relaxed);
        if (host.hasBpm)
        {
            transport.bpm.store (host.bpm, std::memory_order_relaxed);
            transport.hasTempo.store (true, std::memory_order_relaxed);
        }
        if (host.hasPpq)
            transport.ppq.store (host.ppq, std::memory_order_relaxed);
        transport.numerator.store (host.numerator, std::memory_order_relaxed);
        transport.denominator.store (host.denominator, std::memory_order_relaxed);
    }

    // Input meter (decaying peak).
    float peak = 0.0f;
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = 0; ch < numIn; ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    inputPeak.store (juce::jmax (peak, inputPeak.load (std::memory_order_relaxed) * peakDecayPerBlock),
                     std::memory_order_relaxed);

    processCapture (buffer, host);
    processPreview (buffer, host);
}

void SecondOpinionProcessor::processCapture (const juce::AudioBuffer<float>& buffer, const HostPosition& host) noexcept
{
    audioInCapture.store (true);   // seq_cst: pairs with the worker's check in drainCapture()
    int s = captureRaw.load();

    if (s == rawArmed && (! host.hasPlayHead || host.playing))
    {
        stampHasTempo.store (host.hasBpm);
        stampHasPpq.store (host.hasPpq);
        stampHasLastBar.store (host.hasLastBarStart);
        stampBpm.store (host.bpm);
        stampPpq.store (host.ppq);
        stampLastBar.store (host.lastBarStart);
        stampNum.store (host.numerator);
        stampDen.store (host.denominator);
        std::int64_t sampleTime = 0;
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
                if (const auto t = pos->getTimeInSamples())
                    sampleTime = *t;
        stampSampleTime.store (sampleTime);

        so::audio::CaptureStamp stamp;
        stamp.hasTempo = host.hasBpm;
        stamp.bpm = host.bpm;
        stamp.numerator = host.numerator;
        stamp.denominator = host.denominator;
        stamp.sampleRate = currentSampleRate;
        const auto limit = juce::jmin ((std::int64_t) so::audio::captureLimitFrames (stamp), capacityFramesAtomic.load());
        limitFrames.store (limit);
        recordedFrames.store (0);
        captureNeedsTempo.store (! (host.hasBpm && host.hasPpq));

        int expected = rawArmed;
        if (captureRaw.compare_exchange_strong (expected, rawRecording))
            s = rawRecording;
        else
            s = expected;
    }

    if (s == rawRecording)
    {
        bool finish = host.hasPlayHead && ! host.playing;   // transport stop

        if (! finish)
        {
            const auto recorded = recordedFrames.load (std::memory_order_relaxed);
            const auto remaining = limitFrames.load (std::memory_order_relaxed) - recorded;
            const auto n = (std::size_t) juce::jlimit<std::int64_t> (0, buffer.getNumSamples(), remaining);
            const int srcChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());

            if (n > 0 && srcChannels > 0)
            {
                const auto pushed = captureFifo.push (buffer.getArrayOfReadPointers(), srcChannels, n);
                if (pushed < n)
                    captureOverflow.store (true, std::memory_order_relaxed);
                recordedFrames.store (recorded + (std::int64_t) n, std::memory_order_release);
            }

            if (recorded + (std::int64_t) n >= limitFrames.load (std::memory_order_relaxed))
                finish = true;
        }

        if (finish)
        {
            int expected = rawRecording;
            captureRaw.compare_exchange_strong (expected, rawFinishing);
        }
    }

    audioInCapture.store (false);
}

void SecondOpinionProcessor::processPreview (juce::AudioBuffer<float>& buffer, const HostPosition& host) noexcept
{
    const bool want = auditioning.load (std::memory_order_relaxed);

    if (auditionRestart.exchange (false))
    {
        synth.reset();
        freeRunBeat = 0.0;
        previewGain.setCurrentAndTargetValue (0.0f);
        previewActive = true;
    }

    const int soundId = previewSoundId.load (std::memory_order_relaxed);
    if (soundId != appliedSoundId)
    {
        synth.setSound ((so::preview::PreviewSynth::Sound) soundId);
        if (appliedSoundId >= 0)
        {
            synth.releaseAll (true);
            synth.resync();
        }
        appliedSoundId = soundId;
    }

    const float levelDb = previewLevelParam != nullptr ? previewLevelParam->load (std::memory_order_relaxed) : -12.0f;
    previewGain.setTargetValue (want ? juce::Decibels::decibelsToGain (levelDb, -60.0f) : 0.0f);

    if (want)
        previewActive = true;
    if (! previewActive)
        return;   // fully bypassed: output == input, bit for bit

    if (! want && ! previewGain.isSmoothing() && previewGain.getCurrentValue() <= 0.0f)
    {
        previewActive = false;
        synth.reset();
        return;
    }

    const auto* snap = previewExchange.acquire();
    const int numSamples = buffer.getNumSamples();
    const int numOut = buffer.getNumChannels();
    const int cap = previewScratch.getNumSamples();
    float* scratchL = previewScratch.getWritePointer (0);
    float* scratchR = previewScratch.getWritePointer (1);

    const bool followHost = host.playing && host.hasPpq && host.hasBpm;
    const double origin = auditionOriginPpq.load (std::memory_order_relaxed);
    const double freeBpm = snap != nullptr ? snap->bpm : 120.0;
    const double lengthBeats = snap != nullptr && snap->lengthBeats > 0.0 ? snap->lengthBeats : 16.0;
    double firstBeat = followHost ? host.ppq - origin : freeRunBeat;

    for (int done = 0; done < numSamples;)
    {
        const int chunk = juce::jmin (numSamples - done, cap);
        juce::FloatVectorOperations::clear (scratchL, chunk);
        juce::FloatVectorOperations::clear (scratchR, chunk);

        if (followHost)
        {
            const double bps = host.bpm / 60.0 / currentSampleRate;
            synth.render (snap, scratchL, scratchR, chunk, host.ppq - origin + (double) done * bps, bps);
        }
        else
        {
            const double bps = freeBpm / 60.0 / currentSampleRate;
            for (int pos = 0; pos < chunk;)
            {
                if (freeRunBeat >= lengthBeats || freeRunBeat < 0.0)
                    freeRunBeat = std::fmod (std::fmod (freeRunBeat, lengthBeats) + lengthBeats, lengthBeats);
                const double toEnd = std::ceil ((lengthBeats - freeRunBeat) / bps);
                const int seg = juce::jlimit (1, chunk - pos, (int) juce::jmin (toEnd, 1.0e9));
                synth.render (snap, scratchL + pos, scratchR + pos, seg, freeRunBeat, bps);
                freeRunBeat += (double) seg * bps;
                pos += seg;
            }
        }

        if (numOut >= 2)
        {
            float* outL = buffer.getWritePointer (0, done);
            float* outR = buffer.getWritePointer (1, done);
            for (int i = 0; i < chunk; ++i)
            {
                const float g = previewGain.getNextValue();
                outL[i] += scratchL[i] * g;
                outR[i] += scratchR[i] * g;
            }
        }
        else if (numOut == 1)
        {
            float* out = buffer.getWritePointer (0, done);
            for (int i = 0; i < chunk; ++i)
                out[i] += 0.5f * (scratchL[i] + scratchR[i]) * previewGain.getNextValue();
        }

        done += chunk;
    }

    previewExchange.release();
    auditionBeatPos.store (firstBeat, std::memory_order_relaxed);
}

// ---- Capture: message thread and worker ------------------------------------------------------

void SecondOpinionProcessor::ensureCaptureCapacity (std::size_t frames)
{
    for (int ch = 0; ch < captureChannels; ++ch)
        if (captureBuffer[(std::size_t) ch].size() < frames)
            captureBuffer[(std::size_t) ch].assign (frames, 0.0f);
    capacityFramesAtomic.store ((std::int64_t) captureBuffer[0].size());
}

void SecondOpinionProcessor::armCapture()
{
    std::lock_guard<std::mutex> lock (captureMutex);
    const int s = captureRaw.load();
    if (s == rawArmed || s == rawRecording || s == rawFinishing)
        return;

    pendingClip.reset();
    captureWritePos = 0;
    captureOverflow.store (false);
    captureNeedsTempo.store (false);
    recordedFrames.store (0);

    // Discard anything left in the FIFO (consumer side, serialised by the mutex).
    float* discard[2] { drainScratch[0].data(), drainScratch[1].data() };
    while (captureFifo.availableToRead() > 0 && ! drainScratch[0].empty())
        captureFifo.pop (discard, drainScratch[0].size());

    // Grow the buffer if the current tempo needs more than prepareToPlay reserved (64 bars, <= 3 min).
    const double bpm = transport.hasTempo.load() ? transport.bpm.load() : 0.0;
    ensureCaptureCapacity (so::audio::captureCapacityFrames (currentSampleRate, bpm, transport.numerator.load(),
                                                             transport.denominator.load()));
    captureRaw.store (rawArmed);
}

void SecondOpinionProcessor::stopCapture()
{
    int expected = rawArmed;
    if (captureRaw.compare_exchange_strong (expected, rawIdle))
        return;
    expected = rawRecording;
    captureRaw.compare_exchange_strong (expected, rawFinishing);
}

SecondOpinionProcessor::CaptureState SecondOpinionProcessor::captureState() const noexcept
{
    switch (captureRaw.load())
    {
        case rawArmed:     return CaptureState::Armed;
        case rawRecording:
        case rawFinishing: return CaptureState::Recording;
        case rawDone:      return CaptureState::Done;
        default:           return CaptureState::Idle;
    }
}

float SecondOpinionProcessor::captureProgressBars() const noexcept
{
    const double bpm = stampHasTempo.load() ? stampBpm.load() : 120.0;
    const double barQn = so::audio::barLengthQuarters (stampNum.load(), stampDen.load());
    const double seconds = (double) recordedFrames.load() / currentSampleRate;
    return (float) (seconds * bpm / 60.0 / barQn);
}

void SecondOpinionProcessor::drainCapture()
{
    std::lock_guard<std::mutex> lock (captureMutex);
    const int s = captureRaw.load();
    if (s != rawRecording && s != rawFinishing)
        return;

    auto moveAll = [this]
    {
        const auto capacity = captureBuffer[0].size();
        for (;;)
        {
            const auto avail = captureFifo.availableToRead();
            if (avail == 0)
                break;

            if (captureWritePos < capacity)
            {
                float* dst[2] { captureBuffer[0].data() + captureWritePos,
                                captureChannels > 1 ? captureBuffer[1].data() + captureWritePos : nullptr };
                captureWritePos += captureFifo.pop (dst, juce::jmin (avail, capacity - captureWritePos));
            }
            else
            {
                float* discard[2] { drainScratch[0].data(), drainScratch[1].data() };
                captureFifo.pop (discard, juce::jmin (avail, drainScratch[0].size()));
            }
        }
    };

    moveAll();
    if (s != rawFinishing)
        return;

    // The audio thread may still be inside a push it started before the state changed.
    while (audioInCapture.load())
        std::this_thread::yield();
    moveAll();

    so::audio::CaptureStamp stamp;
    stamp.hasTempo = stampHasTempo.load();
    stamp.hasPpq = stampHasPpq.load();
    stamp.bpm = stampBpm.load();
    stamp.firstSamplePpq = stampPpq.load();
    stamp.hasLastBarStart = stampHasLastBar.load();
    stamp.lastBarStartPpq = stampLastBar.load();
    stamp.numerator = stampNum.load();
    stamp.denominator = stampDen.load();
    stamp.sampleRate = currentSampleRate;
    const auto aligned = so::audio::alignToBars (stamp);
    lastBar1Ppq.store (aligned.bar1Ppq);

    if (captureWritePos == 0)
    {
        captureRaw.store (rawIdle);
        return;
    }

    const float* channels[2] { captureBuffer[0].data(), captureChannels > 1 ? captureBuffer[1].data() : nullptr };
    pendingClip.emplace (so::audio::downmixToMono (channels, captureChannels, captureWritePos, currentSampleRate),
                         aligned.grid);
    auditionOriginPpq.store (aligned.bar1Ppq);
    captureRaw.store (rawDone);
}

std::optional<SecondOpinionProcessor::CapturedClip> SecondOpinionProcessor::takeCapturedClip()
{
    std::lock_guard<std::mutex> lock (captureMutex);
    if (captureRaw.load() != rawDone)
        return std::nullopt;

    auto out = std::move (pendingClip);
    pendingClip.reset();
    captureRaw.store (rawIdle);
    return out;
}

// ---- Preview: message thread ------------------------------------------------------------------

void SecondOpinionProcessor::setPreviewSnapshot (const std::vector<so::midi::MidiLane>& lanes, double bpm,
                                                 double lengthBeats)
{
    previewExchange.publish (std::make_shared<const so::preview::PreviewSnapshot> (
        so::preview::makePreviewSnapshot (lanes, bpm, lengthBeats)));
}

void SecondOpinionProcessor::startAudition()
{
    previewExchange.collect();
    auditionRestart.store (true);
    auditioning.store (true);
}

void SecondOpinionProcessor::stopAudition()
{
    auditioning.store (false);
}

void SecondOpinionProcessor::setPreviewSound (PreviewSound s)
{
    previewSoundId.store ((int) s);
    session.setProperty ("previewSound", (int) s, nullptr);
}

juce::AudioProcessorEditor* SecondOpinionProcessor::createEditor()
{
    return new SecondOpinionEditor (*this);
}

void SecondOpinionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("SecondOpinionState");
    root.setProperty ("stateVersion", stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (session.createCopy(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void SecondOpinionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Must survive garbage input (pluginval fuzzes this): validate every step, never assert.
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("SecondOpinionState"))
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto loaded = root.getChildWithName ("Session");
    if (loaded.isValid())
    {
        auto copy = loaded.createCopy();
        migrateSession (copy);
        session.copyPropertiesAndChildrenFrom (copy, nullptr);

        const int sound = session.getProperty ("previewSound", (int) PreviewSound::Keys);
        previewSoundId.store (juce::jlimit (0, 2, sound));
    }
}

void SecondOpinionProcessor::migrateSession (juce::ValueTree& tree)
{
    const int from = tree.getProperty ("stateVersion", 0);

    // Version 0 → 1: nothing stored yet, only stamp the version. Future migrations chain here.
    if (from < 1)
        tree.setProperty ("stateVersion", 1, nullptr);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SecondOpinionProcessor();
}
