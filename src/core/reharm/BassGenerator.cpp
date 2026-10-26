#include "BassGenerator.h"

#include "Harmony.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace so::reharm
{

using theory::wrapPc;

int BassGenerator::place (int pc, int near, int lo, int hi)
{
    const double centre = 0.5 * (lo + hi);
    int best = -1;
    double bestCost = 1.0e300;
    for (int n = lo; n <= hi; ++n)
    {
        if (wrapPc (n) != wrapPc (pc))
            continue;
        const double c = std::abs (n - near) + 0.15 * std::abs (n - centre);
        if (c < bestCost)
        {
            bestCost = c;
            best = n;
        }
    }
    if (best < 0)
        best = lo + wrapPc (pc - lo);
    return best;
}

namespace
{
    double rolePenalty (const theory::Chord& c, int pc)
    {
        const int rel = wrapPc (pc - c.root.pc());
        if (rel == 0) return 0.0;
        if (rel == 3 || rel == 4) return 1.5;
        if (rel == 10 || rel == 11) return 3.0;
        if (rel == 7) return 3.5;
        return 4.0;
    }

    int inversionBass (const theory::Chord& c, int prev, const Settings& s, const theory::Key& key)
    {
        int best = -1;
        double bestCost = 1.0e300;
        for (const auto& iv : theory::info (c.quality).intervals)
        {
            if (iv.degree == 9)
                continue;
            const int pc = wrapPc (c.root.pc() + iv.semitones);
            const int n = BassGenerator::place (pc, prev, s.bassLow, s.bassHigh);
            const int move = std::abs (n - prev);
            double cost = move + rolePenalty (c, pc);
            if (move >= 1 && move <= 2)
                cost -= 1.5;
            if (c.root.pc() == key.tonic.pc() && pc != c.root.pc())
                cost += 2.0;
            if (cost < bestCost)
            {
                bestCost = cost;
                best = n;
            }
        }
        return best;
    }

    midi::MidiNote note (int pitch, double start, double len, int vel)
    {
        midi::MidiNote m;
        m.pitch = pitch;
        m.startBeat = start;
        m.lengthBeats = len;
        m.velocity = vel;
        return m;
    }
}

BassLine BassGenerator::generate (const std::vector<BassSpan>& spans, const Settings& settingsIn,
                                  const theory::Key& key, int beatsPerBar)
{
    const auto s = settingsIn.sanitized();
    const int lo = s.bassLow, hi = s.bassHigh;
    const int centre = (lo + hi) / 2;
    const int bpb = std::max (1, beatsPerBar);
    const int pedalPitch = place (s.pedalPc >= 0 ? s.pedalPc : key.tonic.pc(), centre, lo, hi);

    BassLine out;
    out.spanBass.resize (spans.size());

    // 1. The note on each span's downbeat.
    int prev = centre;
    for (size_t i = 0; i < spans.size(); ++i)
    {
        const auto& sp = spans[i];
        int p = 0;
        if (sp.locked)
            p = place (sp.chord.bassPc(), centre, lo, hi);
        else if (s.bassMotion == BassMotion::Pedal)
            p = pedalPitch;
        else if (s.bassMotion == BassMotion::Inversions && ! sp.chord.isSlash() && ! sp.keepBass && i > 0)
            p = inversionBass (sp.chord, prev, s, key);
        else
            p = place (sp.chord.bassPc(), prev, lo, hi);
        out.spanBass[i] = p;
        prev = p;
    }

    // 2. Rhythm per style.
    if (s.bassMotion == BassMotion::Pedal)
    {
        const double hold = s.pedalBars * bpb;
        size_t i = 0;
        while (i < spans.size())
        {
            if (spans[i].locked)
            {
                out.notes.push_back (note (out.spanBass[i], spans[i].startBeat, spans[i].beats, 100));
                ++i;
                continue;
            }
            const double rs = spans[i].startBeat;
            size_t j = i;
            while (j < spans.size() && ! spans[j].locked)
                ++j;
            const double re = spans[j - 1].startBeat + spans[j - 1].beats;
            for (double t = rs; t < re - 1.0e-9; t += hold)
                out.notes.push_back (note (pedalPitch, t, std::min (hold, re - t), 100));
            i = j;
        }
        return out;
    }

    for (size_t i = 0; i < spans.size(); ++i)
    {
        const auto& sp = spans[i];
        const int p = out.spanBass[i];
        const bool walk = s.bassMotion == BassMotion::Walking && ! sp.locked && sp.beats >= 2.0 - 1.0e-9;
        if (! walk)
        {
            out.notes.push_back (note (p, sp.startBeat, sp.beats, 100));
            continue;
        }

        const int count = (int) std::floor (sp.beats + 1.0e-9);
        const double tail = sp.beats - count;
        const int target = spans.size() > 1 ? out.spanBass[(i + 1) % spans.size()] : p;
        const auto tones = sp.chord.chordTonePcs();

        int cur = p;
        for (int k = 0; k < count; ++k)
        {
            int pitch = p;
            const std::string nk = std::to_string (i) + ":" + std::to_string (k);
            if (k == count - 1 && k > 0)
            {
                // Approach tone into the next chord's bass on the last beat.
                const double r = unitNoise (s.seed, "walk-approach:" + nk);
                const int below = target - 1, above = target + 1;
                const int step = key.inScale (wrapPc (target - 2)) ? target - 2 : target + 2;
                const int fifth = target + 7 <= hi ? target + 7 : target - 5;
                pitch = r < 0.35 ? below : r < 0.6 ? above : r < 0.85 ? step : fifth;
                if (pitch < lo || pitch > hi || pitch == cur)
                    pitch = (below >= lo && below != cur) ? below : above;
                pitch = std::clamp (pitch, lo, hi);
            }
            else if (k > 0)
            {
                if (k % bpb == 0)
                    pitch = p;
                else
                {
                    // Walk through chord tones, heading toward the next bass note.
                    const double r = unitNoise (s.seed, "walk-tone:" + nk);
                    const size_t idx = tones.size() > 2 ? (r < 0.5 ? 1 : 2) : tones.size() - 1;
                    const int pc = tones[std::min (idx, tones.size() - 1)];
                    const int aim = cur + (target > cur ? 3 : target < cur ? -3 : 0);
                    pitch = place (pc, aim, lo, hi);
                    if (pitch == cur)
                        pitch = place (tones[0], aim + 5, lo, hi);
                }
            }
            const double len = (k == count - 1) ? 1.0 + tail : 1.0;
            out.notes.push_back (note (pitch, sp.startBeat + k, len, k == 0 ? 100 : 86));
            cur = pitch;
        }
    }
    return out;
}

} // namespace so::reharm
