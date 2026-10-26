#include "MidiWriter.h"

#include <algorithm>
#include <cmath>

namespace so::midi
{

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    void put32 (Bytes& b, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back ((std::uint8_t) (v >> s)); }
    void put16 (Bytes& b, std::uint16_t v) { b.push_back ((std::uint8_t) (v >> 8)); b.push_back ((std::uint8_t) v); }

    void putVarLen (Bytes& b, std::uint32_t v)
    {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = (std::uint8_t) (v & 0x7f);
        while ((v >>= 7) != 0)
            buf[n++] = (std::uint8_t) ((v & 0x7f) | 0x80);
        while (n > 0)
            b.push_back (buf[--n]);
    }

    struct Event
    {
        std::uint32_t tick;
        int order;          // note-offs before note-ons at the same tick
        Bytes data;
    };

    Bytes trackChunk (std::vector<Event> events)
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
        {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.order < b.order;
        });

        Bytes body;
        std::uint32_t last = 0;
        for (const auto& e : events)
        {
            putVarLen (body, e.tick - last);
            body.insert (body.end(), e.data.begin(), e.data.end());
            last = e.tick;
        }

        // End of track
        body.insert (body.end(), { 0x00, 0xff, 0x2f, 0x00 });

        Bytes chunk { 'M', 'T', 'r', 'k' };
        put32 (chunk, (std::uint32_t) body.size());
        chunk.insert (chunk.end(), body.begin(), body.end());
        return chunk;
    }

    Event nameEvent (const std::string& name)
    {
        Bytes d { 0xff, 0x03 };
        putVarLen (d, (std::uint32_t) name.size());
        d.insert (d.end(), name.begin(), name.end());
        return { 0, -1, d };
    }

    std::uint32_t toTicks (double beats, int ppq)
    {
        return (std::uint32_t) std::llround (std::max (0.0, beats) * ppq);
    }
}

std::vector<std::uint8_t> writeSmf (const std::vector<MidiLane>& lanes, const MidiFileSpec& spec)
{
    Bytes out { 'M', 'T', 'h', 'd' };
    put32 (out, 6);
    put16 (out, 1);
    put16 (out, (std::uint16_t) (lanes.size() + 1));
    put16 (out, (std::uint16_t) spec.ppq);

    // Conductor track: tempo + time signature.
    {
        std::vector<Event> ev;
        ev.push_back (nameEvent ("Second Opinion"));

        const auto usPerQuarter = (std::uint32_t) std::llround (60'000'000.0 / std::max (1.0, spec.bpm));
        ev.push_back ({ 0, 0, { 0xff, 0x51, 0x03, (std::uint8_t) (usPerQuarter >> 16), (std::uint8_t) (usPerQuarter >> 8), (std::uint8_t) usPerQuarter } });

        int denPow = 0;
        for (int d = spec.denominator; d > 1; d >>= 1) ++denPow;
        ev.push_back ({ 0, 0, { 0xff, 0x58, 0x04, (std::uint8_t) spec.numerator, (std::uint8_t) denPow, 24, 8 } });

        const auto chunk = trackChunk (std::move (ev));
        out.insert (out.end(), chunk.begin(), chunk.end());
    }

    for (const auto& lane : lanes)
    {
        std::vector<Event> ev;
        ev.push_back (nameEvent (lane.name));

        for (const auto& n : lane.notes)
        {
            const auto ch = (std::uint8_t) (n.channel & 0x0f);
            const auto pitch = (std::uint8_t) std::clamp (n.pitch, 0, 127);
            const auto vel = (std::uint8_t) std::clamp (n.velocity, 1, 127);
            const auto on = toTicks (n.startBeat, spec.ppq);
            const auto off = std::max (on + 1, toTicks (n.startBeat + n.lengthBeats, spec.ppq));

            ev.push_back ({ on, 1, { (std::uint8_t) (0x90 | ch), pitch, vel } });
            ev.push_back ({ off, 0, { (std::uint8_t) (0x80 | ch), pitch, 0 } });
        }

        const auto chunk = trackChunk (std::move (ev));
        out.insert (out.end(), chunk.begin(), chunk.end());
    }

    return out;
}

} // namespace so::midi
