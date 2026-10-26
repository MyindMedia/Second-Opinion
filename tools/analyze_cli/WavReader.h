#pragma once

// Minimal RIFF/WAVE reader for the analysis CLI (no JUCE): PCM 8/16/24/32-bit and IEEE float
// 32/64-bit, any channel count (downmixed to mono), WAVE_FORMAT_EXTENSIBLE included.

#include "core/analysis/AnalysisTypes.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace so::tools
{

inline bool readWav (const std::string& path, so::analysis::AudioClip& out, std::string& error)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
    {
        error = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> d ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());

    auto u16 = [&] (size_t p) { return (uint32_t) d[p] | ((uint32_t) d[p + 1] << 8); };
    auto u32 = [&] (size_t p) { return u16 (p) | (u16 (p + 2) << 16); };

    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int format = 0, channels = 0, bits = 0;
    double rate = 0.0;
    size_t dataPos = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= d.size();)
    {
        const uint32_t len = u32 (p + 4);
        const size_t body = p + 8;
        if (std::memcmp (d.data() + p, "fmt ", 4) == 0 && body + 16 <= d.size())
        {
            format = (int) u16 (body);
            channels = (int) u16 (body + 2);
            rate = (double) u32 (body + 4);
            bits = (int) u16 (body + 14);
            if (format == 0xFFFE && len >= 26 && body + 26 <= d.size())
                format = (int) u16 (body + 24);   // sub-format GUID starts with the format tag
        }
        else if (std::memcmp (d.data() + p, "data", 4) == 0)
        {
            dataPos = body;
            dataLen = std::min<size_t> (len, d.size() - body);
            break;
        }
        p = body + len + (len & 1u);
    }

    if (dataPos == 0 || channels < 1 || rate <= 0.0 || ! (format == 1 || format == 3))
    {
        error = "unsupported WAV (need PCM or float)";
        return false;
    }
    const int bytes = bits / 8;
    if (bytes < 1 || (format == 3 && bits != 32 && bits != 64) || (format == 1 && bits > 32))
    {
        error = "unsupported bit depth";
        return false;
    }

    const size_t frames = dataLen / ((size_t) bytes * (size_t) channels);
    out.sampleRate = rate;
    out.samples.assign (frames, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const unsigned char* s = d.data() + dataPos + (i * (size_t) channels + (size_t) c) * (size_t) bytes;
            double v = 0.0;
            if (format == 3 && bits == 32)
            {
                float x;
                std::memcpy (&x, s, 4);
                v = x;
            }
            else if (format == 3)
            {
                double x;
                std::memcpy (&x, s, 8);
                v = x;
            }
            else if (bits == 8)
                v = ((int) s[0] - 128) / 128.0;
            else
            {
                int32_t x = 0;
                for (int k = 0; k < bytes; ++k)
                    x |= (int32_t) s[k] << (8 * k);
                const int shift = 32 - bits;
                x = (int32_t) ((uint32_t) x << shift) >> shift;   // sign-extend
                v = x / (double) (1u << (bits - 1));
            }
            acc += v;
        }
        out.samples[i] = (float) (acc / channels);
    }
    return true;
}

} // namespace so::tools
