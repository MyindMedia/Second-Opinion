#include "KeyEstimator.h"

#include <algorithm>
#include <cmath>

namespace so::analysis
{

namespace
{
    constexpr std::array<double, 12> kMajor { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    constexpr std::array<double, 12> kMinor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    constexpr double kSoftmaxTemperature = 0.04;

    // Tonal-centre evidence, in correlation units (before kEvidenceGain). The profile correlation
    // of relative major and minor keys differs mostly by which triad is loudest, which a loop can
    // easily invert (F minor loops full of Ab, C and Eb), so these cues decide between them.
    constexpr float kFirstChordBonus = 0.05f;   // root-position tonic first
    constexpr float kLastChordBonus = 0.06f;    // root-position tonic last
    constexpr float kInvertedTonicFactor = 0.3f;// an inverted tonic (I6) is a weak arrival
    constexpr float kCadenceBonus = 0.06f;      // per full-strength cadence into the tonic
    constexpr float kCadenceCap = 0.15f;
    constexpr float kTonicTimeWeight = 0.08f;   // times the tonic chord's share of chord time
    constexpr float kBassTonicWeight = 0.08f;   // times the tonic's share of bass time
    constexpr float kEvidenceGain = 2.0f;
    constexpr float kEvidenceWindow = 0.3f;     // the relative must be at least this close

    using theory::QualityFamily;

    QualityFamily familyOf (const theory::Chord& c) { return theory::info (c.quality).family; }

    /** The chord is the key's tonic triad (any inversion or extension of it). */
    bool isTonic (const theory::Chord& c, const theory::Key& key)
    {
        if (c.root.pc() != key.tonic.pc())
            return false;
        const auto f = familyOf (c);
        return key.mode == theory::Mode::Minor ? f == QualityFamily::Minor : f == QualityFamily::Major;
    }

    /** Strength (0..1) of `from` as a cadential chord resolving to the tonic of `key`. */
    float cadenceStrength (const theory::Chord& from, const theory::Key& key)
    {
        const int iv = theory::wrapPc (from.root.pc() - key.tonic.pc());
        const auto f = familyOf (from);
        const bool majorish = f == QualityFamily::Major || f == QualityFamily::Dominant || f == QualityFamily::Suspended;
        const bool minor = key.mode == theory::Mode::Minor;
        if (iv == 7 && majorish)
            return 1.0f;                                   // V(7), Vsus -> I / i
        if (iv == 11 && f == QualityFamily::Diminished)
            return 0.7f;                                   // vii° -> I / i
        if (minor && iv == 10 && (f == QualityFamily::Major || f == QualityFamily::Suspended))
            return 0.8f;                                   // bVII -> i (Aeolian cadence)
        if (minor && iv == 7 && f == QualityFamily::Minor)
            return 0.4f;                                   // v -> i
        if (! minor && iv == 5 && f == QualityFamily::Major)
            return 0.5f;                                   // IV -> I (plagal)
        if (minor && iv == 5 && f == QualityFamily::Minor)
            return 0.4f;                                   // iv -> i
        return 0.0f;
    }

    double pearson (const std::array<double, 12>& x, const std::array<double, 12>& profile, int tonic)
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            mx += x[(size_t) i];
            my += profile[(size_t) i];
        }
        mx /= 12.0;
        my /= 12.0;

        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = x[(size_t) theory::wrapPc (i + tonic)] - mx;
            const double b = profile[(size_t) i] - my;
            sxy += a * b;
            sxx += a * a;
            syy += b * b;
        }
        return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    }
}

theory::Key KeyEstimator::keyFor (int pc, theory::Mode mode)
{
    using theory::Letter;
    struct S { Letter l; int acc; };
    static constexpr S major[12] { { Letter::C, 0 }, { Letter::D, -1 }, { Letter::D, 0 }, { Letter::E, -1 },
                                   { Letter::E, 0 }, { Letter::F, 0 },  { Letter::F, 1 }, { Letter::G, 0 },
                                   { Letter::A, -1 }, { Letter::A, 0 }, { Letter::B, -1 }, { Letter::B, 0 } };
    static constexpr S minor[12] { { Letter::C, 0 }, { Letter::C, 1 },  { Letter::D, 0 }, { Letter::E, -1 },
                                   { Letter::E, 0 }, { Letter::F, 0 },  { Letter::F, 1 }, { Letter::G, 0 },
                                   { Letter::G, 1 }, { Letter::A, 0 },  { Letter::B, -1 }, { Letter::B, 0 } };
    const auto& s = (mode == theory::Mode::Major ? major : minor)[theory::wrapPc (pc)];
    return { { s.l, s.acc }, mode };
}

KeyEstimate KeyEstimator::estimate (const std::array<double, 12>& h)
{
    KeyEstimate e;
    e.key = keyFor (0, theory::Mode::Major);

    double total = 0.0;
    for (double v : h)
        total += v;
    if (total <= 0.0)
        return e;

    int best = 0;
    for (int k = 0; k < 24; ++k)
    {
        e.correlation[(size_t) k] = (float) pearson (h, k < 12 ? kMajor : kMinor, k % 12);
        if (e.correlation[(size_t) k] > e.correlation[(size_t) best])
            best = k;
    }

    double z = 0.0;
    for (float c : e.correlation)
        z += std::exp ((c - e.correlation[(size_t) best]) / kSoftmaxTemperature);

    e.key = keyFor (best % 12, best < 12 ? theory::Mode::Major : theory::Mode::Minor);
    e.confidence = (float) (1.0 / z);
    return e;
}

float KeyEstimator::tonalCentreEvidence (const theory::Key& key, const std::vector<ChordSegment>& segs)
{
    if (segs.empty())
        return 0.0f;

    // 1 for a root-position tonic, kInvertedTonicFactor for an inverted one, else 0.
    auto tonicWeight = [&] (const theory::Chord& c)
    {
        if (! isTonic (c, key))
            return 0.0f;
        return c.isSlash() ? kInvertedTonicFactor : 1.0f;
    };

    float bonus = kFirstChordBonus * tonicWeight (segs.front().chord)
                + kLastChordBonus * tonicWeight (segs.back().chord);

    // Cadences into the tonic, plus the loop wrap from the last chord back to the first.
    float cad = 0.0f;
    for (size_t i = 1; i < segs.size(); ++i)
        if (! isTonic (segs[i - 1].chord, key))
            cad += tonicWeight (segs[i].chord) * cadenceStrength (segs[i - 1].chord, key);
    if (segs.size() > 2 && ! isTonic (segs.back().chord, key))
        cad += 0.75f * tonicWeight (segs.front().chord) * cadenceStrength (segs.back().chord, key);
    bonus += std::min (kCadenceCap, kCadenceBonus * cad);

    double total = 0.0, tonicTime = 0.0, bassTonic = 0.0;
    for (const auto& s : segs)
    {
        total += s.beats;
        tonicTime += s.beats * tonicWeight (s.chord);
        if (s.chord.bassPc() == key.tonic.pc())
            bassTonic += s.beats;
    }
    if (total > 0.0)
        bonus += kTonicTimeWeight * (float) (tonicTime / total) + kBassTonicWeight * (float) (bassTonic / total);

    return bonus;
}

KeyEstimate KeyEstimator::estimateWithChords (const std::vector<BeatFeatures>& beats,
                                              const std::vector<ChordSegment>& segments)
{
    auto e = estimate (histogram (beats, &segments));
    if (segments.empty() || e.confidence <= 0.0f)
        return e;

    // Only the profile winner and its relative compete on tonal-centre evidence: the profile is
    // reliable about the scale (and so about everything but the relative), and cadential cues
    // alone would happily crown the dominant of a loop that ends on V.
    int best = 0;
    for (int k = 1; k < 24; ++k)
        if (e.correlation[(size_t) k] > e.correlation[(size_t) best])
            best = k;
    const int relative = best < 12 ? 12 + (best + 9) % 12 : (best - 12 + 3) % 12;
    if (e.correlation[(size_t) relative] >= e.correlation[(size_t) best] - kEvidenceWindow)
    {
        for (int k : { best, relative })
        {
            const auto key = keyFor (k % 12, k < 12 ? theory::Mode::Major : theory::Mode::Minor);
            e.correlation[(size_t) k] += kEvidenceGain * tonalCentreEvidence (key, segments);
        }
        if (e.correlation[(size_t) relative] > e.correlation[(size_t) best])
            best = relative;
    }

    double z = 0.0;
    for (float c : e.correlation)
        z += std::exp ((c - e.correlation[(size_t) best]) / kSoftmaxTemperature);
    e.key = keyFor (best % 12, best < 12 ? theory::Mode::Major : theory::Mode::Minor);
    e.confidence = (float) (1.0 / z);
    return e;
}

std::array<double, 12> KeyEstimator::histogram (const std::vector<BeatFeatures>& beats,
                                                const std::vector<ChordSegment>* segments)
{
    std::array<double, 12> h {};

    for (const auto& b : beats)
    {
        if (b.silent)
            continue;
        const auto up = normalizeChroma (b.chroma, 0.15f);
        const auto lo = normalizeChroma (b.bass, 0.2f);
        for (size_t i = 0; i < 12; ++i)
            h[i] += up[i] + 0.5 * lo[i];
    }

    if (segments != nullptr && ! segments->empty())
    {
        for (size_t i = 0; i < segments->size(); ++i)
        {
            const auto& s = (*segments)[i];
            double w = s.beats * 0.5;
            if (i == 0 || i + 1 == segments->size())
                w *= 1.5;
            for (int pc : s.chord.chordTonePcs())
                h[(size_t) pc] += w;
            h[(size_t) s.chord.root.pc()] += w;
            if (s.chord.isSlash())
                h[(size_t) s.chord.bassPc()] += 0.5 * w;
        }
    }

    return h;
}

} // namespace so::analysis
