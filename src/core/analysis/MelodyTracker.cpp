#include "MelodyTracker.h"

#include "FFT.h"

#include <algorithm>
#include <cmath>

namespace so::analysis
{

namespace
{
    /** Prefix sums of x^2 so any window energy is O(1); indices past the end count as silence. */
    struct EnergyIndex
    {
        std::vector<double> prefix;

        explicit EnergyIndex (const std::vector<float>& x) : prefix (x.size() + 1, 0.0)
        {
            for (size_t i = 0; i < x.size(); ++i)
                prefix[i + 1] = prefix[i] + (double) x[i] * x[i];
        }

        double sum (long long start, long long len) const
        {
            const auto n = (long long) prefix.size() - 1;
            const long long a = std::clamp (start, 0LL, n);
            const long long b = std::clamp (start + len, 0LL, n);
            return prefix[(size_t) b] - prefix[(size_t) a];
        }

        double rms (long long start, long long len) const
        {
            return len > 0 ? std::sqrt (std::max (0.0, sum (start, len)) / (double) len) : 0.0;
        }
    };

    float median (std::vector<float> v)
    {
        if (v.empty())
            return 0.0f;
        const auto mid = v.begin() + (long) (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return *mid;
    }
}

std::vector<PitchFrame> MelodyTracker::pitchTrack (const std::vector<float>& audio, double sr,
                                                   const std::atomic<bool>* cancel,
                                                   const std::function<void (float)>& progress) const
{
    std::vector<PitchFrame> frames;
    if (audio.empty() || sr <= 0.0)
        return frames;

    const int W = std::max (64, options.windowSize);
    const int hop = std::max (1, options.hopSize);
    const int tauMax = (int) std::ceil (sr / std::max (20.0, options.minHz));
    const int tauMin = std::max (2, (int) std::floor (sr / std::max (options.minHz * 1.5, options.maxHz)));
    const FFT fft (FFT::orderFor (W + tauMax + 1));
    const int N = fft.size();

    const EnergyIndex energy (audio);
    const auto len = (long long) audio.size();
    const size_t numFrames = audio.size() / (size_t) hop + 1;
    frames.reserve (numFrames);

    std::vector<FFT::Complex> z ((size_t) N), r ((size_t) N);
    std::vector<double> d ((size_t) tauMax + 2), cmnd ((size_t) tauMax + 2);

    for (size_t f = 0; f < numFrames; ++f)
    {
        if ((f & 31) == 0)
        {
            if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                return frames;
            if (progress)
                progress ((float) f / (float) numFrames);
        }

        const long long s = (long long) f * hop;
        PitchFrame pf;
        pf.timeSec = ((double) s + W * 0.5) / sr;
        const double e0 = energy.sum (s, W);
        pf.rms = (float) std::sqrt (std::max (0.0, e0) / W);

        if (e0 <= 1e-12)
        {
            frames.push_back (pf);
            continue;
        }

        // z = a + i b with a = x[s, s+W) and b = x[s, s+W+tauMax): one FFT gives both spectra.
        for (int i = 0; i < N; ++i)
        {
            const long long j = s + i;
            const float x = (j < len) ? audio[(size_t) j] : 0.0f;
            const float a = i < W ? x : 0.0f;
            const float b = i < W + tauMax ? x : 0.0f;
            z[(size_t) i] = { a, b };
        }
        fft.forward (z.data());

        for (int k = 0; k < N; ++k)
        {
            const auto zk = z[(size_t) k];
            const auto zc = std::conj (z[(size_t) ((N - k) % N)]);
            const FFT::Complex A = 0.5f * (zk + zc);
            const FFT::Complex B = FFT::Complex (0.0f, -0.5f) * (zk - zc);
            r[(size_t) k] = std::conj (A) * B;
        }
        fft.inverse (r.data());

        // Difference function and cumulative-mean normalisation (YIN steps 2 and 3).
        double running = 0.0;
        cmnd[0] = 1.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const double et = energy.sum (s + tau, W);
            d[(size_t) tau] = std::max (0.0, e0 + et - 2.0 * (double) r[(size_t) tau].real());
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
        }

        // Absolute threshold, then walk down to the local minimum (YIN step 4).
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < options.yinThreshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best > 0)
        {
            const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const double den = a - 2.0 * b + c;
            const double off = std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -1.0, 1.0) : 0.0;
            const double period = best + off;
            const double hz = sr / period;
            pf.midi = (float) (69.0 + 12.0 * std::log2 (hz / 440.0) - options.tuningCents / 100.0);
            pf.confidence = (float) std::clamp (1.0 - b, 0.0, 1.0);
            pf.voiced = true;
        }

        frames.push_back (pf);
    }

    // Loudness gates: absolute and relative to the loudest frame.
    float maxRms = 0.0f;
    for (const auto& pf : frames)
        maxRms = std::max (maxRms, pf.rms);
    const float gate = std::max (std::pow (10.0f, options.silenceDb / 20.0f),
                                 maxRms * std::pow (10.0f, options.relativeGateDb / 20.0f));
    for (auto& pf : frames)
        if (pf.rms < gate)
            pf.voiced = false;

    if (progress)
        progress (1.0f);
    return frames;
}

std::vector<NoteEvent> MelodyTracker::track (const std::vector<float>& audio, double sr,
                                             const std::atomic<bool>* cancel,
                                             const std::function<void (float)>& progress) const
{
    const auto frames = pitchTrack (audio, sr, cancel, progress);
    std::vector<NoteEvent> notes;
    if (frames.empty() || (cancel != nullptr && cancel->load (std::memory_order_relaxed)))
        return notes;

    const EnergyIndex energy (audio);
    const int W = std::max (64, options.windowSize);
    const int hop = std::max (1, options.hopSize);
    const int envLen = std::max (16, (int) std::lround (sr * 0.005));

    float maxRms = 0.0f;
    for (const auto& f : frames)
        maxRms = std::max (maxRms, f.rms);

    struct Run { size_t first, last; bool afterGap; bool endsAtGap; };
    std::vector<Run> runs;

    // 1. Segment voiced frames: gaps, held pitch changes (with hysteresis) and re-attacks.
    bool inNote = false, gapBefore = true;
    size_t start = 0, lastVoiced = 0;
    int cur = 0, candidate = 0, candidateRun = 0, unvoicedRun = 0;
    float peak = 0.0f, dipRms = 0.0f;
    bool inDip = false;
    size_t dipFrame = 0;

    auto close = [&] (size_t last, bool atGap)
    {
        if (last >= start)
            runs.push_back ({ start, last, gapBefore, atGap });
        inNote = false;
    };

    for (size_t i = 0; i < frames.size(); ++i)
    {
        const auto& f = frames[i];
        if (! f.voiced)
        {
            ++unvoicedRun;
            if (inNote && unvoicedRun >= 2)
            {
                close (lastVoiced, true);
                gapBefore = true;
            }
            continue;
        }

        unvoicedRun = 0;
        const int rounded = (int) std::lround (f.midi);

        if (! inNote)
        {
            inNote = true;
            start = i;
            cur = rounded;
            candidateRun = 0;
            peak = f.rms;
            inDip = false;
            lastVoiced = i;
            continue;
        }

        if (std::abs (f.midi - (float) cur) > 0.5f + options.hysteresis)
        {
            if (candidateRun > 0 && rounded == candidate)
                ++candidateRun;
            else
            {
                candidate = rounded;
                candidateRun = 1;
            }

            if (candidateRun >= options.changeFrames)
            {
                const size_t newStart = i + 1 - (size_t) candidateRun;
                close (newStart - 1, false);
                gapBefore = false;
                inNote = true;
                start = newStart;
                cur = candidate;
                candidateRun = 0;
                peak = f.rms;
                inDip = false;
            }
        }
        else
            candidateRun = 0;

        // Re-attack on the same pitch: level falls well under the note's peak, then jumps back.
        if (! inDip && f.rms < 0.35f * peak)
        {
            inDip = true;
            dipRms = f.rms;
            dipFrame = i;
        }
        else if (inDip)
        {
            if (f.rms < dipRms)
            {
                dipRms = f.rms;
                dipFrame = i;
            }
            else if (f.rms > 0.7f * peak && dipFrame > start)
            {
                close (dipFrame, true);
                gapBefore = true;
                inNote = true;
                start = dipFrame + 1;
                peak = f.rms;
                inDip = false;
            }
        }
        peak = std::max (peak, f.rms);
        lastVoiced = i;
    }
    if (inNote)
        close (lastVoiced, true);

    // 2. Turn runs into notes with refined onset and offset times.
    long long prevEndSample = 0;
    for (const auto& run : runs)
    {
        std::vector<float> pitches, rmsv;
        double conf = 0.0;
        for (size_t i = run.first; i <= run.last; ++i)
        {
            if (! frames[i].voiced)
                continue;
            pitches.push_back (frames[i].midi);
            rmsv.push_back (frames[i].rms);
            conf += frames[i].confidence;
        }
        if (pitches.empty())
            continue;

        const float noteRms = median (rmsv);
        const double thr = 0.3 * noteRms;

        const long long s0 = (long long) run.first * hop;
        const long long s1 = (long long) run.last * hop;
        double onset = frames[run.first].timeSec;
        double offset = frames[run.last].timeSec;

        if (run.afterGap)
        {
            const long long from = std::max (s0 - W / 2, prevEndSample);
            for (long long i = from; i < s0 + W; i += 8)
                if (energy.rms (i, envLen) >= thr)
                {
                    onset = (double) (i + envLen / 2) / sr;
                    break;
                }
        }
        if (run.endsAtGap)
        {
            for (long long i = s1 + W + W / 2; i > s1; i -= 8)
                if (energy.rms (i, envLen) >= thr)
                {
                    offset = (double) (i + envLen / 2) / sr;
                    break;
                }
        }
        if (offset <= onset)
            continue;

        prevEndSample = (long long) (offset * sr);

        if (offset - onset < options.minNoteSec)
            continue;

        NoteEvent ne;
        ne.pitch = (int) std::lround (median (pitches));
        ne.startSec = onset;
        ne.endSec = offset;
        ne.velocity = maxRms > 0.0f ? std::clamp (0.2f + 0.8f * noteRms / maxRms, 0.0f, 1.0f) : 0.0f;
        ne.confidence = (float) (conf / (double) pitches.size());
        notes.push_back (ne);
    }

    // Keep the list strictly monophonic in time.
    for (size_t i = 1; i < notes.size(); ++i)
        if (notes[i - 1].endSec > notes[i].startSec)
            notes[i - 1].endSec = notes[i].startSec;

    return notes;
}

std::vector<NoteEvent> skyline (const std::vector<NoteEvent>& notes)
{
    std::vector<NoteEvent> sorted = notes;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const NoteEvent& a, const NoteEvent& b)
    {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch > b.pitch;
    });

    std::vector<NoteEvent> out;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto& n = sorted[i];
        const double dur = n.endSec - n.startSec;
        if (dur <= 0.0)
            continue;

        double covered = 0.0;
        for (size_t j = 0; j < sorted.size(); ++j)
        {
            const auto& o = sorted[j];
            if (j == i || o.pitch <= n.pitch)
                continue;
            if (o.startSec >= n.endSec)
                break;
            covered += std::max (0.0, std::min (n.endSec, o.endSec) - std::max (n.startSec, o.startSec));
        }
        if (covered <= 0.5 * dur)
            out.push_back (n);
    }
    return out;
}

std::vector<MelodyNote> toMelodyNotes (const std::vector<NoteEvent>& notes, const BeatGrid& grid,
                                       double totalBeats, double q)
{
    std::vector<MelodyNote> out;
    if (q <= 0.0)
        q = 0.25;

    std::vector<NoteEvent> sorted = notes;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const NoteEvent& a, const NoteEvent& b)
                      { return a.startSec < b.startSec; });

    for (const auto& n : sorted)
    {
        const double sb = std::round (grid.secToBeat (n.startSec) / q) * q;
        double eb = std::round (grid.secToBeat (n.endSec) / q) * q;
        if (sb < 0.0 || sb >= totalBeats)
            continue;
        eb = std::min (std::max (eb, sb + q), totalBeats);

        if (! out.empty())
        {
            auto& prev = out.back();
            if (prev.startBeat >= sb)
            {
                if (n.pitch > prev.pitch)   // same slot: keep the higher (melodic) note
                    prev = { n.pitch, sb, eb - sb };
                continue;
            }
            if (prev.startBeat + prev.beats > sb)
                prev.beats = sb - prev.startBeat;
        }
        out.push_back ({ n.pitch, sb, eb - sb });
    }
    return out;
}

} // namespace so::analysis
