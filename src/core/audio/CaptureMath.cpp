#include "CaptureMath.h"

#include <algorithm>
#include <cmath>

namespace so::audio
{

double barLengthQuarters (int numerator, int denominator) noexcept
{
    const int num = std::max (numerator, 1);
    const int den = denominator > 0 ? denominator : 4;
    return (double) num * 4.0 / (double) den;
}

BarAlignment alignToBars (const CaptureStamp& s, double fallbackBpm, double snapToleranceSamples) noexcept
{
    BarAlignment out;
    const double barQn = barLengthQuarters (s.numerator, s.denominator);
    out.grid.beatsPerBar = std::max (1, (int) std::lround (barQn));

    if (! s.hasTempo || ! s.hasPpq || ! (s.bpm > 0.0) || ! (s.sampleRate > 0.0))
    {
        out.grid.bpm = s.hasTempo && s.bpm > 0.0 ? s.bpm : fallbackBpm;
        out.grid.downbeatSec = 0.0;
        out.bar1Ppq = s.hasPpq ? s.firstSamplePpq : 0.0;
        return out;
    }

    out.grid.bpm = s.bpm;
    const double qnPerSample = s.bpm / 60.0 / s.sampleRate;
    const double tolQn = snapToleranceSamples * qnPerSample;

    // Bar origin: the host's last bar start if it gave one (handles meter changes), else bars from PPQ 0.
    const double origin = s.hasLastBarStart ? s.lastBarStartPpq : 0.0;
    const double barsIn = (s.firstSamplePpq - origin) / barQn;
    double barIndex = std::floor (barsIn);
    double barStart = origin + barIndex * barQn;

    if (s.firstSamplePpq - barStart > tolQn)   // started mid-bar: beat 0 is the next bar line
    {
        barIndex += 1.0;
        barStart = origin + barIndex * barQn;
    }

    out.bar1Ppq = barStart;
    out.grid.downbeatSec = (barStart - s.firstSamplePpq) * 60.0 / s.bpm;   // may be a hair below 0 when snapped
    return out;
}

std::size_t captureLimitFrames (const CaptureStamp& s, int maxBars, double maxSeconds) noexcept
{
    double seconds = maxSeconds;
    if (s.hasTempo && s.bpm > 0.0)
        seconds = std::min (seconds, (double) maxBars * barLengthQuarters (s.numerator, s.denominator) * 60.0 / s.bpm);
    return (std::size_t) std::max (0.0, std::floor (seconds * s.sampleRate));
}

std::size_t captureCapacityFrames (double sampleRate, double bpm, int numerator, int denominator,
                                   int maxBars, double maxSeconds) noexcept
{
    double seconds = maxSeconds;
    if (bpm > 0.0)
        seconds = std::min (seconds, (double) maxBars * barLengthQuarters (numerator, denominator) * 60.0 / bpm);
    return (std::size_t) std::max (1.0, std::ceil (seconds * std::max (sampleRate, 1.0)));
}

analysis::AudioClip downmixToMono (const float* const* channels, int numChannels, std::size_t numFrames,
                                   double sampleRate)
{
    analysis::AudioClip clip;
    clip.sampleRate = sampleRate;
    if (channels == nullptr || numChannels <= 0)
        return clip;

    clip.samples.assign (channels[0], channels[0] + numFrames);
    if (numChannels == 1)
        return clip;

    for (int ch = 1; ch < numChannels; ++ch)
        for (std::size_t i = 0; i < numFrames; ++i)
            clip.samples[i] += channels[ch][i];

    const float scale = 1.0f / (float) numChannels;
    for (auto& v : clip.samples)
        v *= scale;
    return clip;
}

} // namespace so::audio
