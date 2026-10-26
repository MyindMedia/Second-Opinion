#include "ChromaExtractor.h"

#include "FFT.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace so::analysis
{

namespace
{
    constexpr float kPeakFloorRelative = 1.0e-3f;   // -60 dB below the frame's strongest peak
    constexpr float kPeakFloorAbsolute = 1.0e-5f;   // sine amplitude, about -100 dBFS
    constexpr float kHarmonicSubtract = 0.8f;       // fraction of the 1/h harmonic model removed
    constexpr int kNumHarmonics = 10;
    constexpr float kMaxBassBrightness = 0.9f;      // partial h at most 0.9^log2(h) of the fundamental
    constexpr float kLogGain = 30.0f;               // log1p(kLogGain * amplitude)

    constexpr double kTuningMinHz = 80.0;
    constexpr double kTuningMaxHz = 1600.0;
    constexpr float kTuningPeakFloor = 0.1f;         // -20 dB below the frame's strongest peak
    constexpr double kTuningMinResultant = 0.15;    // below this the deviations look random

    double midiToHz (double m, double refHz = 440.0) { return refHz * std::pow (2.0, (m - 69.0) / 12.0); }
}

ChromaSettings ChromaSettings::forQuality (AnalysisQuality q) noexcept
{
    switch (q)
    {
        case AnalysisQuality::Fast:     return { 4096, 2048 };
        case AnalysisQuality::Balanced: return { 8192, 1024 };
        case AnalysisQuality::Accurate: return { 8192, 512 };
    }
    return {};
}

std::array<float, kNumPitches> ChromaExtractor::pitchSalience (const std::vector<float>& mags, int fftSize,
                                                               double sampleRate, float ampScale,
                                                               float tuningCents)
{
    std::array<float, kNumPitches> p {};

    const double refHz = 440.0 * std::pow (2.0, tuningCents / 1200.0);
    const double binHz = sampleRate / fftSize;
    const int maxBin = (int) mags.size() - 2;
    const int kLo = std::max (1, (int) std::floor (midiToHz (kLowestPitch - 0.5, refHz) / binHz));
    const int kHi = std::min (maxBin, (int) std::ceil (midiToHz (kHighestPitch + 0.5, refHz) / binHz) + 1);
    if (kHi <= kLo)
        return p;

    float frameMax = 0.0f;
    for (int k = kLo; k <= kHi; ++k)
        frameMax = std::max (frameMax, mags[(size_t) k]);

    const float threshold = std::max (frameMax * kPeakFloorRelative, kPeakFloorAbsolute / ampScale);

    // 1. Spectral peaks, refined by parabolic interpolation on log magnitude, to the nearest MIDI pitch.
    for (int k = kLo; k <= kHi; ++k)
    {
        const float m = mags[(size_t) k];
        if (m <= threshold || m <= mags[(size_t) k - 1] || m < mags[(size_t) k + 1])
            continue;

        const double a = std::log (mags[(size_t) k - 1] + 1e-20);
        const double b = std::log ((double) m + 1e-20);
        const double c = std::log (mags[(size_t) k + 1] + 1e-20);
        const double den = a - 2.0 * b + c;
        const double off = std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        const double amp = std::exp (b - 0.25 * (a - c) * off);

        const double hz = ((double) k + off) * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (hz / refHz);
        const auto note = (int) std::lround (midi);
        if (note < kLowestPitch || note > kHighestPitch)
            continue;

        p[(size_t) (note - kLowestPitch)] += (float) amp * ampScale;
    }

    // 2. Harmonic suppression: walk upwards, removing the expected partials of each residual
    //    note (amplitude ~ 1/h) from the pitches they land on. A real note that coincides with a
    //    partial keeps whatever exceeds the model.
    static const std::array<int, kNumHarmonics + 1> offsets = []
    {
        std::array<int, kNumHarmonics + 1> o {};
        for (int h = 1; h <= kNumHarmonics; ++h)
            o[(size_t) h] = (int) std::lround (12.0 * std::log2 ((double) h));
        return o;
    }();

    //    Bass notes (below the chroma split) get an adaptive model: bass sounds range from
    //    nearly pure to overdriven, and a bright bass's 3rd partial (a 12th up) otherwise reads as
    //    a real fifth in the chord register, turning Ab/C into Cm. Their brightness b is the
    //    median of the estimates from partials 2, 3 and 4 (a real chord note on one of them
    //    inflates one estimate, phase cancellation against one deflates another), and partial h
    //    is modelled as r * b^log2(h), which is the 1/h model at b = 0.5.
    static const std::array<float, kNumHarmonics + 1> log2h = []
    {
        std::array<float, kNumHarmonics + 1> l {};
        for (int h = 1; h <= kNumHarmonics; ++h)
            l[(size_t) h] = (float) std::log2 ((double) h);
        return l;
    }();

    for (int i = 0; i < kNumPitches; ++i)
    {
        const float r = p[(size_t) i];
        if (r <= 0.0f)
            continue;

        float bright = 0.5f;
        if (kLowestPitch + i < kBassSplit && i + offsets[5] < kNumPitches)
        {
            std::array<float, 4> b {};
            for (int h = 2; h <= 5; ++h)
                b[(size_t) h - 2] = std::pow (p[(size_t) (i + offsets[(size_t) h])] / r, 1.0f / log2h[(size_t) h]);
            std::sort (b.begin(), b.end());
            bright = std::clamp (b[1], 0.5f, kMaxBassBrightness);
        }

        for (int h = 2; h <= kNumHarmonics; ++h)
        {
            const int t = i + offsets[(size_t) h];
            if (t >= kNumPitches)
                break;
            const float model = bright <= 0.5f ? 1.0f / (float) h : std::pow (bright, log2h[(size_t) h]);
            auto& v = p[(size_t) t];
            v = std::max (0.0f, v - kHarmonicSubtract * r * model);
        }
    }

    return p;
}

float ChromaExtractor::estimateTuning (const std::vector<float>& audio, double sampleRate,
                                      const std::atomic<bool>* cancel)
{
    if (audio.empty() || sampleRate <= 0.0)
        return 0.0f;

    // 8192 points at 22,050 Hz: 2.7 Hz bins, so even 80 Hz peaks are resolved to a few cents
    // after interpolation. Half-overlapping frames are plenty for one global number.
    const int order = FFT::orderFor (sampleRate > 30000.0 ? 16384 : 8192);
    const FFT fft (order);
    const int n = fft.size();
    const int hop = n / 2;

    std::vector<float> window ((size_t) n), frame ((size_t) n), mags ((size_t) n / 2 + 1);
    for (int i = 0; i < n; ++i)
    {
        const double x = 2.0 * std::numbers::pi * i / n;
        window[(size_t) i] = (float) (0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x));
    }
    std::vector<FFT::Complex> scratch;

    const double binHz = sampleRate / n;
    const int kLo = std::max (2, (int) std::floor (kTuningMinHz / binHz));
    const int kHi = std::min (n / 2 - 2, (int) std::ceil (kTuningMaxHz / binHz));

    double sumC = 0.0, sumS = 0.0, sumW = 0.0;
    const auto len = audio.size();
    for (size_t start = 0; start + (size_t) n <= len || start == 0; start += (size_t) hop)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            return 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const size_t j = start + (size_t) i;
            frame[(size_t) i] = (j < len ? audio[j] : 0.0f) * window[(size_t) i];
        }
        fft.realMagnitudes (frame.data(), mags.data(), scratch);

        float frameMax = 0.0f;
        for (int k = kLo; k <= kHi; ++k)
            frameMax = std::max (frameMax, mags[(size_t) k]);
        if (frameMax <= 1e-9f)
            continue;

        for (int k = kLo; k <= kHi; ++k)
        {
            const float m = mags[(size_t) k];
            if (m < frameMax * kTuningPeakFloor || m <= mags[(size_t) k - 1] || m < mags[(size_t) k + 1])
                continue;
            const double a = std::log (mags[(size_t) k - 1] + 1e-20);
            const double b = std::log ((double) m + 1e-20);
            const double c = std::log (mags[(size_t) k + 1] + 1e-20);
            const double den = a - 2.0 * b + c;
            const double off = std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            const double midi = 69.0 + 12.0 * std::log2 (((double) k + off) * binHz / 440.0);
            const double dev = midi - std::round (midi);              // -0.5..0.5 semitone
            const double w = m / frameMax;
            sumC += w * std::cos (2.0 * std::numbers::pi * dev);
            sumS += w * std::sin (2.0 * std::numbers::pi * dev);
            sumW += w;
        }
        if (start + (size_t) n > len)
            break;
    }

    if (sumW <= 0.0 || std::hypot (sumC, sumS) / sumW < kTuningMinResultant)
        return 0.0f;
    return (float) (100.0 * std::atan2 (sumS, sumC) / (2.0 * std::numbers::pi));
}

ChromaFrames ChromaExtractor::process (const std::vector<float>& audio, double sampleRate,
                                       const std::vector<NoteEvent>* notes, float noteWeight,
                                       const std::atomic<bool>* cancel,
                                       const std::function<void (float)>& progress) const
{
    ChromaFrames out;
    out.sampleRate = sampleRate;
    out.settings = settings;

    if (audio.empty() || sampleRate <= 0.0)
        return out;

    const int order = FFT::orderFor (std::max (16, settings.windowSize));
    const FFT fft (order);
    const int n = fft.size();
    const int hop = std::max (1, settings.hopSize);

    std::vector<float> window ((size_t) n);
    double winSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = 2.0 * std::numbers::pi * i / n;
        window[(size_t) i] = (float) (0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x));   // Blackman
        winSum += window[(size_t) i];
    }
    const auto ampScale = (float) (2.0 / winSum);   // spectral peak -> sine amplitude

    const size_t numFrames = audio.size() / (size_t) hop + 1;
    out.times.reserve (numFrames);
    out.chroma.reserve (numFrames);
    out.bass.reserve (numFrames);
    out.energy.reserve (numFrames);

    std::vector<float> frame ((size_t) n), mags ((size_t) n / 2 + 1);
    std::vector<FFT::Complex> scratch;
    const auto len = (long long) audio.size();

    for (size_t f = 0; f < numFrames; ++f)
    {
        if ((f & 15) == 0)
        {
            if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                return out;
            if (progress)
                progress ((float) f / (float) numFrames);
        }

        const long long start = (long long) f * hop - n / 2;
        double sq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const long long j = start + i;
            const float s = (j >= 0 && j < len) ? audio[(size_t) j] : 0.0f;
            sq += (double) s * s;
            frame[(size_t) i] = s * window[(size_t) i];
        }

        fft.realMagnitudes (frame.data(), mags.data(), scratch);
        const auto sal = pitchSalience (mags, n, sampleRate, ampScale, settings.tuningCents);

        Chroma up {}, lo {};
        for (int i = 0; i < kNumPitches; ++i)
        {
            const int midi = kLowestPitch + i;
            const float v = std::log1p (kLogGain * sal[(size_t) i]);
            (midi >= kBassSplit ? up : lo)[(size_t) (midi % 12)] += v;
        }

        const double t = (double) f * hop / sampleRate;

        if (notes != nullptr && ! notes->empty() && noteWeight > 0.0f)
        {
            Chroma nu {}, nl {};
            float nmax = 0.0f;
            for (const auto& ne : *notes)
            {
                if (t < ne.startSec || t >= ne.endSec)
                    continue;
                const float w = ne.velocity * ne.confidence;
                auto& dst = ne.pitch >= kBassSplit ? nu : nl;
                const auto pc = (size_t) (((ne.pitch % 12) + 12) % 12);
                dst[pc] += w;
                nmax = std::max (nmax, dst[pc]);
            }

            float fmax = 0.0f;
            for (int i = 0; i < 12; ++i)
                fmax = std::max ({ fmax, up[(size_t) i], lo[(size_t) i] });

            if (nmax > 0.0f && fmax > 0.0f)
            {
                const float g = noteWeight * fmax / nmax;
                for (size_t i = 0; i < 12; ++i)
                {
                    up[i] += g * nu[i];
                    lo[i] += g * nl[i];
                }
            }
        }

        out.times.push_back (t);
        out.chroma.push_back (up);
        out.bass.push_back (lo);
        out.energy.push_back ((float) std::sqrt (sq / n));
    }

    if (progress)
        progress (1.0f);

    return out;
}

} // namespace so::analysis
