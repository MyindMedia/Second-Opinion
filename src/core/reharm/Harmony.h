#pragma once

// Chord transformations, the tension metric and melody compatibility used across the engine.

#include "../analysis/AnalysisTypes.h"
#include "../theory/Chord.h"
#include "../theory/Key.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace so::reharm
{

using theory::Chord;
using theory::ChordQuality;
using theory::Key;

// ----------------------------------------------------------------------------------------------
// Construction and transformations
// ----------------------------------------------------------------------------------------------

/** Spells a root for `key`, avoiding E#, B#, Cb, Fb and double accidentals. */
theory::SpelledPitch spellRoot (int pc, const Key& key);

Chord makeChord (int rootPc, ChordQuality q, const Key& key);

bool isDominantShaped (const Chord& c) noexcept;   // 7, 9, 7sus4
bool isMajorFamily (const Chord& c) noexcept;      // maj, 6, maj7, add9, maj9
bool isMinorFamily (const Chord& c) noexcept;      // m, m6, m7, m9

/** Tritone substitute of a dominant (G7 -> Db7, G9 -> Db9, G7sus4 -> Db7). */
Chord tritoneSubstitute (const Chord& dominant, const Key& key);

/** V7 of the target (Dm -> A7). */
Chord secondaryDominantOf (const Chord& target, const Key& key, bool ninth = false);

/** Neo-Riemannian transforms on the triad core. A seventh is kept as maj7 / m7 when `keepSeventh`. */
Chord neoP (const Chord& c, const Key& key, bool keepSeventh = false);   // C -> Cm
Chord neoL (const Chord& c, const Key& key, bool keepSeventh = false);   // C -> Em, Cm -> Ab
Chord neoR (const Chord& c, const Key& key, bool keepSeventh = false);   // C -> Am, Cm -> Eb

/** Chromatic (non-diatonic) mediants: same quality, root a third away, at least one common tone. */
std::vector<Chord> chromaticMediants (const Chord& c, const Key& key);

/** Diatonic chord on scale degree 0..6 (natural minor in minor keys). */
Chord diatonicChord (const Key& key, int degree, bool seventh);

int commonTones (const Chord& a, const Chord& b);

/** Pitch-class voice-leading distance: each tone of b to its nearest tone of a (in semitones). */
int pcDistance (const Chord& a, const Chord& b);

// ----------------------------------------------------------------------------------------------
// Tension metric (FR-RH-02). Internal, 0..100.
// ----------------------------------------------------------------------------------------------

int chordTension (const Chord& c, const Key& key);

/** Target tension of a changed chord for the Tension knob. */
double targetChordTension (int tensionKnob) noexcept;

// ----------------------------------------------------------------------------------------------
// Melody compatibility (FR-RH-01 AC2)
// ----------------------------------------------------------------------------------------------

enum class ToneRole { ChordTone, Tension, Avoid };

/** Role of melody pitch class `pc` over `c`. Which tensions count as allowed grows with the knob. */
ToneRole classifyTone (const Chord& c, int pc, int tensionKnob);

/** Strong beats: beats 1 and 3 in 4/4 (every other beat), beat 1 otherwise. */
bool isStrongBeat (double beat, int beatsPerBar) noexcept;

struct MelodyHit
{
    int pitch { 60 };
    double beat { 0.0 };     // where the note is heard inside the span
    double weight { 1.0 };
    bool strong { false };
};

/** Melody notes heard in [start, start + beats). A note sustained into the span counts at its start. */
std::vector<MelodyHit> melodyHits (const std::vector<analysis::MelodyNote>& melody, double start,
                                   double beats, int beatsPerBar);

/** 0..1 melody fit; `strongOk` is false when a strong-beat note is an avoid note. */
double melodyFit (const Chord& c, const std::vector<MelodyHit>& hits, int tensionKnob, bool& strongOk);

/** Melody pitch sounding at `beat`, else the first note starting within `window` beats. */
std::optional<int> melodyAt (const std::vector<analysis::MelodyNote>& melody, double beat, double window);

/** Ordinal name of `pc` relative to the chord: "root", "3rd", "flat 7th", "9th", "sharp 11th". */
std::string degreeName (const Chord& c, int pc);

// ----------------------------------------------------------------------------------------------
// Deterministic hashing for seeded choices
// ----------------------------------------------------------------------------------------------

std::uint64_t mix64 (std::uint64_t x) noexcept;
std::uint64_t hashString (std::uint64_t seed, const std::string& s) noexcept;

/** Uniform 0..1 from a seed and a key. */
double unitNoise (std::uint64_t seed, const std::string& key) noexcept;

} // namespace so::reharm
