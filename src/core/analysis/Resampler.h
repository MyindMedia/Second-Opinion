#pragma once

// Downmix and sample-rate conversion for analysis. Everything downstream runs at 22,050 Hz.

#include <atomic>
#include <vector>

namespace so::analysis
{

/** The rate every analysis stage works at (PRD 7.4). */
inline constexpr double kAnalysisRate = 22050.0;

/** Averages channels into one mono buffer (length of the shortest channel). */
std::vector<float> downmix (const std::vector<std::vector<float>>& channels);

/** Same, from raw channel pointers. */
std::vector<float> downmix (const float* const* channels, int numChannels, size_t numSamples);

/** Band-limited resampling with a Kaiser-windowed sinc (24 zero crossings, beta 8.6).
    When downsampling, the cutoff sits at 94% of the output Nyquist, so at 44.1 kHz -> 22.05 kHz
    the passband reaches about 9 kHz and content above about 11.6 kHz is attenuated by more than
    80 dB. Output length is floor(in.size() * outRate / inRate). Equal rates return a copy.
    Returns an empty buffer when `cancel` becomes true part way. */
std::vector<float> resample (const std::vector<float>& in, double inRate, double outRate = kAnalysisRate,
                             const std::atomic<bool>* cancel = nullptr);

} // namespace so::analysis
