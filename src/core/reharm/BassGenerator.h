#pragma once

// P3-02: bass lane in Root, Inversions, Walking or Pedal style (FR-RH-04).

#include "../midi/MidiWriter.h"
#include "../theory/Chord.h"
#include "../theory/Key.h"
#include "Settings.h"

#include <vector>

namespace so::reharm
{

struct BassSpan
{
    theory::Chord chord;
    double startBeat { 0.0 };
    double beats { 4.0 };
    bool locked { false };
    bool keepBass { false };      // the segment had an explicit slash bass: play chord.bassPc()
};

struct BassLine
{
    std::vector<midi::MidiNote> notes;
    std::vector<int> spanBass;    // pitch sounding at each span's start (the solver's floor)
};

class BassGenerator
{
public:
    /** Explicit slash basses are always kept, except in Pedal. Locked spans hold their own bass note
        in an isolated register so they stay byte-identical whatever surrounds them. */
    static BassLine generate (const std::vector<BassSpan>& spans, const Settings& settings,
                              const theory::Key& key, int beatsPerBar);

    /** Pitch of `pc` inside [lo, hi] closest to `near` (with a gentle pull to the range centre). */
    static int place (int pc, int near, int lo, int hi);
};

} // namespace so::reharm
