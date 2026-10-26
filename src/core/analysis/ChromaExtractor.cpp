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
    constexpr int kNumHarmonics = 8;
    constexpr float kLogGain = 30.0f;               // log1p(kLogGain * amplitude)

    double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
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
                                                               double sampleRate, float ampScale)
{
    std::array<float, kNumPitches> p {};

    const double binHz = sampleRate / fftSize;
    const int maxBin = (int) mags.size() - 2;
    const int kLo = std::max (1, (int) std::floor (midiToHz (kLowestPitch - 0.5) / binHz));
    const int kHi = std::min (maxBin, (int) std::ceil (midiToHz (kHighestPitch + 0.5) / binHz) + 1);
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
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
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

    for (int i = 0; i < kNumPitches; ++i)
    {
        const float r = p[(size_t) i];
        if (r <= 0.0f)
            continue;
        for (int h = 2; h <= kNumHarmonics; ++h)
        {
            const int t = i + offsets[(size_t) h];
            if (t >= kNumPitches)
                break;
            auto& v = p[(size_t) t];
            v = std::max (0.0f, v - kHarmonicSubtract * r / (float) h);
        }
    }

    return p;
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
        const auto sal = pitchSalience (mags, n, sampleRate, ampScale);

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
