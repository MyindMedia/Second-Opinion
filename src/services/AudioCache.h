#pragma once

// Local cache for analysed audio (FR-ST-01 AC3, docs/adr/0011-session-persistence.md).
//
// Captured audio is never written into the DAW project. It goes to
// <userApplicationData>/SecondOpinion/cache/<sha256>.flac and the project stores only the hash.
// Everything here runs on a worker thread except cacheDirectory() and clear(), which are cheap.

#include "core/analysis/AnalysisTypes.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace so::cache
{

/** Minimal SHA-256 (FIPS 180-4). juce_cryptography is not linked into the plugin. */
class Sha256
{
public:
    Sha256() { reset(); }

    void reset()
    {
        h = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
        length = 0;
        used = 0;
    }

    void update (const void* data, size_t size)
    {
        auto* p = static_cast<const std::uint8_t*> (data);
        length += (std::uint64_t) size;
        while (size > 0)
        {
            const auto n = std::min (size, (size_t) 64 - used);
            std::memcpy (block.data() + used, p, n);
            used += n;
            p += n;
            size -= n;
            if (used == 64)
            {
                compress();
                used = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish()
    {
        const auto bits = length * 8u;
        const std::uint8_t one = 0x80, zero = 0;
        update (&one, 1);
        while (used != 56)
            update (&zero, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = (std::uint8_t) (bits >> (56 - 8 * i));
        update (len, 8);

        std::array<std::uint8_t, 32> out {};
        for (size_t i = 0; i < 8; ++i)
            for (size_t b = 0; b < 4; ++b)
                out[i * 4 + b] = (std::uint8_t) (h[i] >> (24 - 8 * b));
        return out;
    }

    static juce::String hex (const std::array<std::uint8_t, 32>& digest)
    {
        return juce::String::toHexString (digest.data(), (int) digest.size(), 0);
    }

    static juce::String of (const void* data, size_t size)
    {
        Sha256 s;
        s.update (data, size);
        return hex (s.finish());
    }

private:
    static std::uint32_t rotr (std::uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }

    void compress()
    {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t) block[(size_t) i * 4] << 24 | (std::uint32_t) block[(size_t) i * 4 + 1] << 16
                 | (std::uint32_t) block[(size_t) i * 4 + 2] << 8 | (std::uint32_t) block[(size_t) i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            const auto s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const auto t1 = hh + (rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto t2 = (rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<std::uint32_t, 8> h {};
    std::array<std::uint8_t, 64> block {};
    std::uint64_t length { 0 };
    size_t used { 0 };
};

/** <userApplicationData>/SecondOpinion/cache (not created). */
inline juce::File cacheDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SecondOpinion")
        .getChildFile ("cache");
}

/** Hash of the audio as analysed: the float samples (little-endian) followed by the sample rate. */
inline juce::String hashClip (const analysis::AudioClip& clip)
{
    Sha256 s;
    if (! clip.samples.empty())
        s.update (clip.samples.data(), clip.samples.size() * sizeof (float));
    const double rate = clip.sampleRate;
    s.update (&rate, sizeof (rate));
    return Sha256::hex (s.finish());
}

/** Writes <dir>/<hash>.flac (mono, 24 bit) unless it already exists. Worker thread. */
inline bool writeClip (const analysis::AudioClip& clip, const juce::String& hash, const juce::File& dir)
{
    if (clip.samples.empty() || hash.isEmpty() || ! dir.createDirectory())
        return false;

    const auto file = dir.getChildFile (hash + ".flac");
    if (file.existsAsFile())
        return true;

    juce::FlacAudioFormat flac;
    const auto tmp = file.getSiblingFile (hash + ".partial");
    tmp.deleteFile();

    bool ok = false;
    {
        std::unique_ptr<juce::OutputStream> stream = tmp.createOutputStream();
        if (stream == nullptr)
            return false;

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate ((double) juce::roundToInt (clip.sampleRate))
                                 .withNumChannels (1)
                                 .withBitsPerSample (24);
        auto writer = flac.createWriterFor (stream, options);
        if (writer != nullptr)
        {
            const float* channels[] { clip.samples.data() };
            ok = writer->writeFromFloatArrays (channels, 1, (int) clip.samples.size());
        }
    }   // the writer (and its stream) flush and close here

    if (ok && tmp.moveFileTo (file))
        return true;
    tmp.deleteFile();
    return false;
}

/** Deletes everything inside the cache folder. Returns the number of files removed. */
inline int clear (const juce::File& dir = cacheDirectory())
{
    int removed = 0;
    if (! dir.isDirectory())
        return 0;
    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto f = entry.getFile();
        if (f.isDirectory() ? f.deleteRecursively() : f.deleteFile())
            ++removed;
    }
    return removed;
}

} // namespace so::cache
