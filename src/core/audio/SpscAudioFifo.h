#pragma once

// Single-producer / single-consumer lock-free FIFO of planar float audio (PRD 7.3, NFR-05).
// The producer is the audio thread, the consumer is the capture drain worker. Headless, no JUCE.
//
// reset() allocates and must only be called while neither side is running. push() and pop() never
// allocate, lock or block. Indices are monotonic 64-bit counters, so full/empty need no spare slot.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace so::audio
{

class SpscAudioFifo
{
public:
    static constexpr int maxChannels = 2;

    /** Allocates room for `capacityFrames` frames of `numChannels` (1 or 2). Not real-time safe. */
    void reset (int numChannels, std::size_t capacityFrames)
    {
        channels = std::clamp (numChannels, 1, maxChannels);
        capacity = std::max<std::size_t> (capacityFrames, 1);
        for (int ch = 0; ch < maxChannels; ++ch)
            data[(std::size_t) ch].assign (ch < channels ? capacity : 0, 0.0f);
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
    }

    int numChannels() const noexcept { return channels; }
    std::size_t capacityFrames() const noexcept { return capacity; }

    /** Frames the consumer can pop right now. */
    std::size_t availableToRead() const noexcept
    {
        return (std::size_t) (writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_relaxed));
    }

    /** Frames the producer can push right now. */
    std::size_t availableToWrite() const noexcept
    {
        const auto used = writeIndex.load (std::memory_order_relaxed) - readIndex.load (std::memory_order_acquire);
        return capacity - (std::size_t) used;
    }

    /** Producer side. Copies up to `numFrames` frames; returns how many fit. If the source has fewer
        channels than the FIFO, the last source channel is repeated; extra source channels are ignored. */
    std::size_t push (const float* const* source, int sourceChannels, std::size_t numFrames) noexcept
    {
        if (capacity == 0 || sourceChannels <= 0 || source == nullptr)
            return 0;

        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);
        const auto free = capacity - (std::size_t) (w - r);
        const auto n = std::min (numFrames, free);
        const auto start = (std::size_t) (w % capacity);
        const auto first = std::min (n, capacity - start);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = source[std::min (ch, sourceChannels - 1)];
            float* dst = data[(std::size_t) ch].data();
            std::copy (src, src + first, dst + start);
            std::copy (src + first, src + n, dst);
        }

        writeIndex.store (w + n, std::memory_order_release);
        return n;
    }

    /** Consumer side. Copies up to `numFrames` frames into `dest[0..numChannels()-1]`. */
    std::size_t pop (float* const* dest, std::size_t numFrames) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        const auto n = std::min (numFrames, (std::size_t) (w - r));
        if (n == 0)
            return 0;

        const auto start = (std::size_t) (r % capacity);
        const auto first = std::min (n, capacity - start);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = data[(std::size_t) ch].data();
            std::copy (src + start, src + start + first, dest[ch]);
            std::copy (src, src + (n - first), dest[ch] + first);
        }

        readIndex.store (r + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> data[maxChannels];
    int channels { 1 };
    std::size_t capacity { 0 };
    alignas (64) std::atomic<std::uint64_t> writeIndex { 0 };
    alignas (64) std::atomic<std::uint64_t> readIndex { 0 };
};

} // namespace so::audio
