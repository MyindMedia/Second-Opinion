#include "TempoEstimator.h"

#include "FFT.h"
#include "Resampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <numeric>

namespace so::analysis
{

namespace
{
    constexpr double analysisRate = 22050.0;
    constexpr int fftOrder = 10;        // 1024
    constexpr int hop = 256;            // 11.6 ms per frame
    constexpr double frameRate = analysisRate / hop;
    constexpr double kChangeWeight = 1.5;   // harmonic change vs onsets in the bar-phase choice
    constexpr double kHalfBeatAmbiguity = 0.8;   // half-beat phase this close to the best is a contender

    struct Envelope
    {
        std::vector<float> all;   // full-band spectral flux
        std::vector<float> low;   // flux below ~200 Hz (kick, bass): marks downbeats
        std::vector<std::array<float, 12>> chroma;   // coarse 180 Hz..2 kHz chroma: chord changes
    };

    // A frame's onset shows up while the window slides over it, centred about half a window
    // before the onset itself; frame times add this back.
    constexpr double latencySec = (double) (1 << fftOrder) / 2.0 / analysisRate;

    Envelope onsetEnvelope (const std::vector<float>& x)
    {
        FFT fft (fftOrder);
        const int n = fft.size();
        const int bins = n / 2 + 1;
        const int lowBins = (int) std::ceil (200.0 * n / analysisRate);

        std::vector<float> window ((size_t) n), frame ((size_t) n), mag ((size_t) bins), prev ((size_t) bins, 0.0f);
        std::vector<int> binPc ((size_t) bins, -1);
        for (int b = 1; b < bins; ++b)
        {
            const double hz = b * analysisRate / n;
            if (hz >= 180.0 && hz <= 2000.0)
                binPc[(size_t) b] = (int) (((std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)) % 12) + 12) % 12);
        }
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) n);
        std::vector<FFT::Complex> scratch;

        Envelope env;
        for (size_t start = 0; start + (size_t) n <= x.size(); start += hop)
        {
            for (int i = 0; i < n; ++i)
                frame[(size_t) i] = x[start + (size_t) i] * window[(size_t) i];
            fft.realMagnitudes (frame.data(), mag.data(), scratch);

            float flux = 0.0f, lowFlux = 0.0f;
            std::array<float, 12> chroma {};
            for (int b = 1; b < bins; ++b)
            {
                const float m = std::log1p (100.0f * mag[(size_t) b]);
                if (binPc[(size_t) b] >= 0)
                    chroma[(size_t) binPc[(size_t) b]] += m;
                const float d = std::max (0.0f, m - prev[(size_t) b]);
                flux += d;
                if (b <= lowBins)
                    lowFlux += d;
                prev[(size_t) b] = m;
            }
            env.all.push_back (flux);
            env.low.push_back (lowFlux);
            env.chroma.push_back (chroma);
        }

        // Remove the slow trend (local mean over ~0.5 s) so sustained loudness doesn't count as onsets.
        auto detrend = [] (std::vector<float>& e)
        {
            const int w = 22;
            std::vector<float> out (e.size());
            double acc = 0.0;
            std::deque<float> q;
            for (size_t i = 0; i < e.size(); ++i)
            {
                q.push_back (e[i]);
                acc += e[i];
                if ((int) q.size() > 2 * w + 1) { acc -= q.front(); q.pop_front(); }
                out[i] = std::max (0.0f, e[i] - (float) (acc / (double) q.size()));
            }
            e.swap (out);
        };
        detrend (env.all);
        detrend (env.low);
        return env;
    }

    double autocorr (const std::vector<float>& e, double lag)
    {
        // Linear interpolation for fractional lags.
        const int l0 = (int) lag;
        const double f = lag - l0;
        double s = 0.0;
        for (size_t i = 0; i + (size_t) l0 + 1 < e.size(); ++i)
            s += e[i] * ((1.0 - f) * e[i + (size_t) l0] + f * e[i + (size_t) l0 + 1]);
        return s / (double) e.size();
    }

    /** Beat phase (the grid offset that collects the most onset energy) and bar phase (which of
        the 4 beats starts the bar) for a given tempo. The bar phase weighs harmonic change
        (chroma novelty across the beat, since chords mostly change on downbeats) above
        low-frequency and overall onsets (kick and bass, but also a backbeat snare). Returns the
        first downbeat in seconds; `firstBeatSec` gets the first beat. */
    double downbeatFor (const Envelope& env, double bpm, double* firstBeatSec)
    {
        const double period = frameRate * 60.0 / bpm;
        const auto frames = (double) env.all.size();

        double bestPhase = 0.0, bestPhaseScore = -1.0;
        for (int p = 0; p < (int) period; ++p)
        {
            double s = 0.0;
            for (double t = p; t < frames; t += period)
                s += env.all[(size_t) t];
            if (s > bestPhaseScore)
            {
                bestPhaseScore = s;
                bestPhase = p;
            }
        }

        auto meanChroma = [&] (double from, double to)
        {
            std::array<double, 12> c {};
            for (auto f = (long long) std::max (0.0, from); f < (long long) std::min (frames, to); ++f)
                for (size_t k = 0; k < 12; ++k)
                    c[k] += env.chroma[(size_t) f][k];
            return c;
        };
        auto novelty = [&] (double t)
        {
            const auto a = meanChroma (t - period, t);
            const auto b = meanChroma (t, t + period);
            double ab = 0.0, aa = 0.0, bb = 0.0;
            for (size_t k = 0; k < 12; ++k)
            {
                ab += a[k] * b[k];
                aa += a[k] * a[k];
                bb += b[k] * b[k];
            }
            return (aa > 0.0 && bb > 0.0) ? 1.0 - ab / std::sqrt (aa * bb) : 0.0;
        };

        // Syncopated parts (strummed eighths, offbeat stabs) can make the half-beat offset collect
        // as much onset energy as the beat. Then the phase where the harmony changes wins.
        {
            const double alt = std::fmod (bestPhase + period / 2.0, period);
            double altScore = 0.0, nowChange = 0.0, altChange = 0.0;
            for (double t = alt; t < frames; t += period)
                altScore += env.all[(size_t) t];
            if (altScore >= kHalfBeatAmbiguity * bestPhaseScore)
            {
                for (double t = bestPhase + period; t + period <= frames; t += period)
                    nowChange += novelty (t);
                for (double t = alt + period; t + period <= frames; t += period)
                    altChange += novelty (t);
                if (altChange > nowChange)
                    bestPhase = alt;
            }
        }

        std::array<double, 4> onset {}, change {};
        double onsetSum = 0.0, changeSum = 0.0;
        for (int b = 0; b < 4; ++b)
        {
            for (double t = bestPhase + b * period; t < frames; t += 4.0 * period)
            {
                onset[(size_t) b] += 2.0 * env.low[(size_t) t] + env.all[(size_t) t];
                if (t >= period && t + period <= frames)
                    change[(size_t) b] += novelty (t);
            }
            onsetSum += onset[(size_t) b];
            changeSum += change[(size_t) b];
        }

        int bestBeat = 0;
        double bestBeatScore = -1.0;
        for (int b = 0; b < 4; ++b)
        {
            double s = onsetSum > 0.0 ? onset[(size_t) b] / onsetSum : 0.0;
            if (changeSum > 1e-6)
                s += kChangeWeight * change[(size_t) b] / changeSum;
            if (s > bestBeatScore)
            {
                bestBeatScore = s;
                bestBeat = b;
            }
        }

        const double beatSec = 60.0 / bpm;
        if (firstBeatSec != nullptr)
            *firstBeatSec = bestPhase / frameRate + latencySec;
        double downbeat = (bestPhase + bestBeat * period) / frameRate + latencySec;
        // The grid starts at the first downbeat; earlier audio becomes a pickup before beat 0.
        while (downbeat - 4.0 * beatSec >= -0.05)
            downbeat -= 4.0 * beatSec;
        return std::max (0.0, downbeat);
    }
}

std::optional<TempoEstimate> estimateTempo (const AudioClip& clip)
{
    if (clip.samples.empty() || clip.sampleRate <= 0.0 || (double) clip.samples.size() / clip.sampleRate < 4.0)
        return std::nullopt;

    const auto x = resample (clip.samples, clip.sampleRate, analysisRate);
    const auto env = onsetEnvelope (x);
    if (env.all.size() < 200)
        return std::nullopt;

    const double energy = std::accumulate (env.all.begin(), env.all.end(), 0.0);
    if (energy <= 1e-6)
        return std::nullopt;

    // Score BPM candidates in 0.5 BPM steps with harmonic summation (beat, 2 beats, half beat)
    // and a log-Gaussian preference centred on 100 BPM (resolves half/double ambiguity gently).
    double bestBpm = 120.0, bestScore = -1.0, sumScores = 0.0;
    int count = 0;
    for (double bpm = 60.0; bpm <= 180.0; bpm += 0.5)
    {
        const double lag = frameRate * 60.0 / bpm;
        double s = autocorr (env.all, lag) + 0.5 * autocorr (env.all, 2.0 * lag) + 0.25 * autocorr (env.all, 4.0 * lag);
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 100.0) / 0.9, 2.0));
        s *= prior;
        sumScores += s;
        ++count;
        if (s > bestScore)
        {
            bestScore = s;
            bestBpm = bpm;
        }
    }

    // Refine to 0.1 BPM around the winner.
    for (double bpm = bestBpm - 0.5; bpm <= bestBpm + 0.5; bpm += 0.1)
    {
        const double lag = frameRate * 60.0 / bpm;
        const double s = (autocorr (env.all, lag) + 0.5 * autocorr (env.all, 2.0 * lag) + 0.25 * autocorr (env.all, 4.0 * lag))
                         * std::exp (-0.5 * std::pow (std::log2 (bpm / 100.0) / 0.9, 2.0));
        if (s > bestScore)
        {
            bestScore = s;
            bestBpm = bpm;
        }
    }

    // Half-tempo check: if the off-beats are nearly as strong as the beats, the real tempo is double.
    if (bestBpm * 2.0 <= 180.0)
    {
        const double lag = frameRate * 60.0 / bestBpm;
        const double onBeat = autocorr (env.all, lag);
        const double half = autocorr (env.all, lag / 2.0);
        if (onBeat > 0.0 && half / onBeat > 0.7)
            bestBpm *= 2.0;
    }

    TempoEstimate est;
    est.bpm = std::round (bestBpm * 10.0) / 10.0;
    // Snap to a whole BPM when it is very close (most productions use integer tempos).
    if (std::abs (est.bpm - std::round (est.bpm)) <= 0.3)
        est.bpm = std::round (est.bpm);
    est.downbeatSec = downbeatFor (env, est.bpm, &est.firstBeatSec);
    const double mean = sumScores / std::max (1, count);
    est.confidence = (float) std::clamp ((bestScore / std::max (mean, 1e-12) - 1.0) / 3.0, 0.0, 1.0);
    return est;
}

BeatGrid chooseFileGrid (const AudioClip& clip, std::optional<double> hostBpm)
{
    BeatGrid grid;
    grid.beatsPerBar = 4;
    grid.bpm = hostBpm.value_or (120.0);
    grid.downbeatSec = 0.0;

    const auto est = estimateTempo (clip);
    if (! est)
        return grid;

    grid.downbeatSec = est->downbeatSec;

    if (hostBpm)
    {
        for (double ratio : { 1.0, 0.5, 2.0 })
        {
            if (std::abs (est->bpm * ratio - *hostBpm) / *hostBpm <= 0.04)
            {
                grid.bpm = *hostBpm;   // the session tempo, the audio agrees
                if (std::abs (est->bpm - *hostBpm) > 1e-9)
                {
                    // Beat and bar phase belong to the tempo actually used (a half-time detection
                    // would otherwise put the downbeat on any of 8 half-beats).
                    const auto env = onsetEnvelope (resample (clip.samples, clip.sampleRate, analysisRate));
                    grid.downbeatSec = downbeatFor (env, *hostBpm, nullptr);
                }
                return grid;
            }
        }
    }

    grid.bpm = est->bpm;
    return grid;
}

void trimTrailingTail (AudioClip& clip, const BeatGrid& grid)
{
    if (clip.sampleRate <= 0.0 || grid.bpm <= 0.0)
        return;

    const double barSec = grid.beatsPerBar * 60.0 / grid.bpm;
    const double startSec = std::max (0.0, grid.downbeatSec);
    const double total = (double) clip.samples.size() / clip.sampleRate;
    const int fullBars = (int) std::floor ((total - startSec) / barSec);
    if (fullBars < 2)
        return;

    auto rms = [&] (double from, double to)
    {
        const auto a = (size_t) std::max (0.0, from * clip.sampleRate);
        const auto b = std::min (clip.samples.size(), (size_t) (to * clip.sampleRate));
        double acc = 0.0;
        for (size_t i = a; i < b; ++i)
            acc += (double) clip.samples[i] * clip.samples[i];
        return b > a ? std::sqrt (acc / (double) (b - a)) : 0.0;
    };

    std::vector<double> levels;
    for (int b = 0; b < fullBars; ++b)
        levels.push_back (rms (startSec + b * barSec, startSec + (b + 1) * barSec));
    auto sorted = levels;
    std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    const double median = sorted[sorted.size() / 2];

    int keepBars = fullBars;
    const double partial = (total - startSec) / barSec - fullBars;
    if (partial >= 0.6 && rms (startSec + fullBars * barSec, total) >= 0.3 * median)
        keepBars = fullBars + 1;   // a real (if short) last bar
    while (keepBars > 1 && keepBars <= fullBars && levels[(size_t) keepBars - 1] < 0.3 * median)
        --keepBars;                // quiet tail bars

    const auto keepSamples = (size_t) std::min ((double) clip.samples.size(), (startSec + keepBars * barSec) * clip.sampleRate);
    clip.samples.resize (keepSamples);
}

} // namespace so::analysis
