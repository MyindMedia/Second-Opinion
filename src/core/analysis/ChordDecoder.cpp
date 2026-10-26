#include "ChordDecoder.h"

#include "../theory/ChordNamer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>

namespace so::analysis
{

using theory::ChordQuality;

namespace
{
    constexpr int kQ = theory::numChordQualities;
    constexpr float kUpperWeight = 2.0f;
    constexpr float kBassWeight = 1.0f;
    constexpr float kChromaFloor = 0.15f;
    constexpr float kBassFloor = 0.2f;
    constexpr float kMinBassShare = 0.04f;

    struct QualityTables
    {
        std::array<std::array<float, 12>, kQ> templ {};    // L2-normalised, rooted at C
        std::array<std::array<float, 12>, kQ> bassCost {}; // penalty for each bass interval above the root
        std::array<unsigned, kQ> mask {};
        std::array<int, kQ> complexity {};
    };

    const QualityTables& tables()
    {
        static const QualityTables t = []
        {
            QualityTables q;
            for (int i = 0; i < kQ; ++i)
            {
                const auto& qi = theory::info ((ChordQuality) i);
                q.mask[(size_t) i] = theory::qualityMask ((ChordQuality) i);
                q.complexity[(size_t) i] = qi.complexity;

                float norm = 0.0f;
                for (int pc = 0; pc < 12; ++pc)
                    if (q.mask[(size_t) i] & (1u << pc))
                    {
                        q.templ[(size_t) i][(size_t) pc] = 1.0f;
                        norm += 1.0f;
                    }
                for (auto& v : q.templ[(size_t) i])
                    v /= std::sqrt (norm);

                for (int rel = 0; rel < 12; ++rel)
                {
                    float cost = 0.45f;   // foreign bass (G/F): allowed, but it has to earn it
                    for (const auto& iv : qi.intervals)
                    {
                        if (iv.semitones % 12 != rel)
                            continue;
                        switch (iv.degree)
                        {
                            case 1:  cost = 0.0f;  break;
                            case 3:  cost = 0.15f; break;
                            case 5:  cost = 0.2f;  break;
                            case 7:  cost = 0.3f;  break;
                            default: cost = 0.35f; break;   // 2, 4, 6, 9 in the bass
                        }
                    }
                    q.bassCost[(size_t) i][(size_t) rel] = cost;
                }
            }
            return q;
        }();
        return t;
    }

    /** Per-beat features prepared once: normalised upper chroma (unit L2) and bass profile. */
    struct Prepared
    {
        std::array<float, 12> upper {};
        std::array<float, 12> bass {};
        bool silent { true };
    };

    Prepared prepare (const BeatFeatures& b)
    {
        Prepared p;
        p.silent = b.silent;
        if (b.silent)
            return p;

        const auto c = normalizeChroma (b.chroma, kChromaFloor);
        float n2 = 0.0f;
        for (float v : c)
            n2 += v * v;
        if (n2 > 0.0f)
            for (size_t i = 0; i < 12; ++i)
                p.upper[i] = c[i] / std::sqrt (n2);

        const float upSum = std::accumulate (b.chroma.begin(), b.chroma.end(), 0.0f);
        const float loSum = std::accumulate (b.bass.begin(), b.bass.end(), 0.0f);
        if (loSum > 0.0f && loSum / (upSum + loSum) >= kMinBassShare)
            p.bass = normalizeChroma (b.bass, kBassFloor);

        return p;
    }

    unsigned scaleMask (const theory::Key& key)
    {
        unsigned m = 0;
        const int t = key.tonic.pc();
        for (int s : key.scaleSemitones())
            m |= 1u << theory::wrapPc (t + s);
        if (key.mode == theory::Mode::Minor)
            m |= 1u << theory::wrapPc (t + 11);   // harmonic-minor leading tone (V, vii°)
        return m;
    }

    unsigned rotate (unsigned mask, int by)
    {
        by = theory::wrapPc (by);
        return ((mask << by) | (mask >> (12 - by))) & 0xFFFu;
    }

    float stateScore (const Prepared& p, const ChordState& s, const ChordDecoderOptions& o, unsigned keyMask)
    {
        if (p.silent)
            return 0.0f;

        const auto& t = tables();
        const auto q = (size_t) s.quality;

        float dot = 0.0f;
        for (int pc = 0; pc < 12; ++pc)
            dot += p.upper[(size_t) theory::wrapPc (s.root + pc)] * t.templ[q][(size_t) pc];

        const int rel = theory::wrapPc (s.bassPc - s.root);
        float sc = kUpperWeight * dot
                 + kBassWeight * p.bass[(size_t) s.bassPc]
                 - t.bassCost[q][(size_t) rel]
                 - o.complexityWeight * (float) t.complexity[q];

        if (keyMask != 0)
        {
            unsigned tones = rotate (t.mask[q], s.root);
            if (t.bassCost[q][(size_t) rel] > 0.4f)
                tones |= 1u << s.bassPc;
            const int total = std::popcount (tones);
            const int outside = std::popcount (tones & ~keyMask);
            sc -= o.keyWeight * (float) outside / (float) total;
        }

        return sc;
    }
}

int ChordDecoder::stateIndex (const ChordState& s) noexcept
{
    return ((int) s.quality * 12 + theory::wrapPc (s.root)) * 12 + theory::wrapPc (s.bassPc);
}

ChordState ChordDecoder::stateAt (int index) noexcept
{
    ChordState s;
    s.bassPc = index % 12;
    s.root = (index / 12) % 12;
    s.quality = (ChordQuality) (index / 144);
    return s;
}

float ChordDecoder::score (const BeatFeatures& beat, const ChordState& s, const ChordDecoderOptions& options)
{
    return stateScore (prepare (beat), s, options, options.key ? scaleMask (*options.key) : 0u);
}

theory::Chord ChordDecoder::toChord (const ChordState& s, const theory::Key* key)
{
    theory::Chord c;
    c.root = theory::SpelledPitch::fromPcAuto (s.root);
    c.quality = s.quality;
    if (theory::wrapPc (s.bassPc) != theory::wrapPc (s.root))
        c.bass = theory::SpelledPitch::fromPcAuto (s.bassPc);

    if (key != nullptr)
        c = theory::ChordNamer::respell (c, theory::NamingOptions::Spelling::Auto, key);
    return c;
}

std::vector<ChordSegment> ChordDecoder::decode (const std::vector<BeatFeatures>& beats,
                                                const ChordDecoderOptions& o,
                                                const std::atomic<bool>* cancel) const
{
    const int numBeats = (int) beats.size();
    int first = 0;
    while (first < numBeats && beats[(size_t) first].silent)
        ++first;
    if (first >= numBeats)
        return {};

    const unsigned keyMask = o.key ? scaleMask (*o.key) : 0u;
    const float invT = 1.0f / std::max (1e-4f, o.temperature);
    constexpr int N = numStates;

    std::vector<float> scores ((size_t) numBeats * N, 0.0f);
    for (int b = first; b < numBeats; ++b)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            return {};
        const auto p = prepare (beats[(size_t) b]);
        if (p.silent)
            continue;
        float* row = scores.data() + (size_t) b * N;
        for (int s = 0; s < N; ++s)
            row[s] = stateScore (p, stateAt (s), o, keyMask);
    }

    // Viterbi. Change cost depends on where the beat sits in the bar; staying is free.
    const int bpb = std::max (1, o.beatsPerBar);
    std::vector<float> delta (N), next (N);
    std::vector<int> back ((size_t) numBeats * N, -1);

    for (int s = 0; s < N; ++s)
        delta[(size_t) s] = scores[(size_t) first * N + (size_t) s] * invT;

    for (int b = first + 1; b < numBeats; ++b)
    {
        if (cancel != nullptr && (b & 7) == 0 && cancel->load (std::memory_order_relaxed))
            return {};

        const int pos = b % bpb;
        const float cost = pos == 0 ? o.changeCostDownbeat
                         : (bpb % 2 == 0 && pos == bpb / 2) ? o.changeCostMidBar
                         : o.changeCostWeak;

        const auto best = (int) std::distance (delta.begin(), std::max_element (delta.begin(), delta.end()));
        const float fromBest = delta[(size_t) best] - cost;
        const float* row = scores.data() + (size_t) b * N;
        int* bp = back.data() + (size_t) b * N;

        for (int s = 0; s < N; ++s)
        {
            const float stay = delta[(size_t) s];
            if (stay >= fromBest || s == best)
            {
                next[(size_t) s] = stay + row[s] * invT;
                bp[s] = s;
            }
            else
            {
                next[(size_t) s] = fromBest + row[s] * invT;
                bp[s] = best;
            }
        }
        std::swap (delta, next);
    }

    std::vector<int> path ((size_t) numBeats, -1);
    path[(size_t) numBeats - 1] = (int) std::distance (delta.begin(), std::max_element (delta.begin(), delta.end()));
    for (int b = numBeats - 1; b > first; --b)
        path[(size_t) b - 1] = back[(size_t) b * N + (size_t) path[(size_t) b]];

    // Merge runs into segments; confidence = softmax posterior of the mean score over sounding beats.
    std::vector<ChordSegment> segments;
    const theory::Key* key = o.key ? &*o.key : nullptr;
    const float invTc = 1.0f / std::max (1e-4f, o.confidenceTemperature);
    std::vector<double> mean (N);

    int start = first;
    for (int b = first + 1; b <= numBeats; ++b)
    {
        if (b < numBeats && path[(size_t) b] == path[(size_t) start])
            continue;

        std::fill (mean.begin(), mean.end(), 0.0);
        int sounding = 0;
        for (int k = start; k < b; ++k)
        {
            if (beats[(size_t) k].silent)
                continue;
            ++sounding;
            const float* row = scores.data() + (size_t) k * N;
            for (int s = 0; s < N; ++s)
                mean[(size_t) s] += row[s];
        }

        ChordSegment seg;
        seg.id = (int) segments.size();
        seg.startBeat = start;
        seg.beats = b - start;
        const int chosen = path[(size_t) start];
        seg.chord = toChord (stateAt (chosen), key);

        if (sounding > 0)
        {
            double mx = -1e30;
            for (auto& m : mean)
            {
                m /= sounding;
                mx = std::max (mx, m);
            }
            double z = 0.0;
            for (const auto& m : mean)
                z += std::exp ((m - mx) * invTc);

            seg.confidence = (float) (std::exp ((mean[(size_t) chosen] - mx) * invTc) / z);

            std::vector<int> order (N);
            std::iota (order.begin(), order.end(), 0);
            std::partial_sort (order.begin(), order.begin() + 4, order.end(), [&] (int a, int c)
            {
                return mean[(size_t) a] != mean[(size_t) c] ? mean[(size_t) a] > mean[(size_t) c] : a < c;
            });
            for (int i = 0; i < 4 && seg.alternatives.size() < 3; ++i)
            {
                if (order[(size_t) i] == chosen)
                    continue;
                seg.alternatives.push_back ({ toChord (stateAt (order[(size_t) i]), key),
                                              (float) (std::exp ((mean[(size_t) order[(size_t) i]] - mx) * invTc) / z) });
            }
        }

        segments.push_back (std::move (seg));
        start = b;
    }

    return segments;
}

} // namespace so::analysis
