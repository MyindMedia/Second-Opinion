# ADR 0006: Reharmonization engine

Status: accepted (Phase 3 P3-01/P3-02, Phase 4 P4-01/P4-02/P4-04)

Headless C++20 in `src/core/reharm/` (namespace `so::reharm`, no JUCE). Entry point:

```cpp
std::vector<Suggestion> generate (const analysis::AnalysisResult&, const Settings&, const std::set<int>& lockedSegmentIds);
Suggestion voiceOriginal (const analysis::AnalysisResult&, const Settings&, const std::set<int>& lockedSegmentIds);
```

## Pipeline

1. **CandidateGenerator (P4-01).** For every unlocked segment and every enabled family it builds
   replacement candidates that always cover the segment's exact duration:
   - Diatonic: same-root extensions and simplifications (7, 9, 6, sus, add9) and diatonic
     substitutes sharing two triad tones (relative, mediant).
   - Borrowed: every non-diatonic chord of the parallel key that keeps the function or two common
     tones (iv, iv6, bVI, bIII, bVII, iiø7 in major), plus the backdoor bVII7 into the tonic.
   - Secondary: V7/x inserted in the last 2 beats before its target, or replacing a chord on the
     same root (Am to A7), and ii-V insertion into the next chord.
   - Tritone subs: subV7 replacing a dominant that falls a fifth (G7 to Db7), and subV7 inserted as a
     chromatic approach chord.
   - Chromatic mediants (non-diatonic, same quality, a third away, one common tone) and
     Neo-Riemannian P, L, R on the triad core.
   Rules: explicit slash basses are kept on replacements and slash segments are never split;
   insertions need 4 beats (2 at Tension 75+) and never sit in front of a chord on the same root.
   Every strong-beat melody note (beats 1 and 3 in 4/4, plus notes sustained into a chord) must be a
   chord tone or an allowed tension for the chord quality; allowed tensions widen with Tension
   (for example b9, #9, b13 on dominants from 35). Candidates more than 30 tension points above
   the target are dropped; the gate is lifted only if fewer than 6 plans survive.
2. **Ranker (P4-02).** Candidates are grouped by idea (technique plus variant, e.g. "modal
   interchange:bVI"). Each group yields plans: the best change wherever the same move recurs, the
   best two, and "everywhere it fits" capped at a third of the segments; pairs of strong groups
   from different techniques add two-idea plans. Plans that replace a chord another change resolves
   into, or that repeat a neighbour's chord, are discarded. Features are scored by `IRanker`
   (the MVP `HeuristicRanker`), then a diverse set is chosen by MMR (lambda 2.5 on 0.6 change
   overlap plus 0.4 technique overlap).

   `score = 3 melodyFit + 3 tensionMatch + 1 voiceLeading + 1.5 novelty + 2 tensionPush + 0.8 jitter(seed)`

   - tensionMatch: `1 - |changedTension - target| / 80`, target `4 + 0.8 * Tension`.
   - tensionPush: `clamp(((Tension - 50) / 50) * (predictedTension - originalTension) / 10, -1, 1)`.
     This term is what makes the set mean move monotonically with the knob.
   - voiceLeading: chord-level pitch-class distance relative to the original (independent of
     Smoothness, so Smoothness never changes which chords are chosen).
   - novelty: rises to 1 at 20% of beats changed and falls after 60%.
   - jitter: a hash of (seed, plan key); Shuffle is a new seed.
3. **BassGenerator (P3-02).** Root (written bass), Inversions (cheapest of root/3rd/5th/7th with a
   step bonus), Walking (quarter notes through chord tones, last beat is a chromatic, scale-step
   or fifth approach into the next bass, chosen by seeded hash), Pedal (one pitch class re-struck
   every N bars). Slash basses are kept in every mode except Pedal. Runs before the voicer: each
   span's bass pitch is a hard floor for the upper voices.
4. **VoiceLeadingSolver (P3-01).** Per chord it enumerates every voicing of exactly `density`
   notes inside the range and above the bass, built from an essential core (3rd or sus tone, 7th
   or 6th, 9th, altered 5th, root unless the bass covers it) plus doublings, with gaps of at most
   an octave and a span limit of `12 + 4 * (density - 2)`. The pruned set (best 2 per outer-voice
   bucket, 64 total) feeds an exact DP. With melody on top, the top voice is fixed to the melody
   note (or the 3rd/6th below), falling back to a soft penalty if no voicing fits.

   | Weight | Value | Term |
   |---|---|---|
   | alpha | `0.15 + 2.85 * Smoothness / 100` | per semitone of total voice movement (sorted voice pairing) |
   | beta | 5 | per parallel 5th or octave (inner pairs and bass against top) |
   | gamma | 0.3 | per semitone the voicing centre sits off the range centre (pulled toward 62) |
   | delta | 1 | spacing: seconds below C3 (+8), low-interval limits (+4), m2 (+1.5, +3 on top), holes, mud, crowding the bass |
   | epsilon | 4 | per semitone of melody-on-top miss (fallback only) |

   Only alpha depends on Smoothness. For a fixed candidate set an exact DP then gives total
   movement that can only go down as alpha rises (sum the two optimality inequalities), which is
   why the Smoothness test is exact rather than statistical.
5. **Locks.** A locked segment is never a candidate, cannot be split, and is voiced in isolation:
   its bass sits nearest the bass-range centre and its voicing is the best static voicing for that
   bass. The DP treats it as a fixed state, so voice leading into and out of it is still optimised
   while its own notes are byte-identical across seeds, Tension, Color and Smoothness.
6. **Explainer (P4-04).** `Explanation{function, roman, technique, techniqueTerm, melodyRelation,
   voiceLeadingNotes}` from `RomanNumeral::analyze` and `ChordNamer`; tritone subs are named by
   where they resolve (subV7/x). Simple text is one plain sentence with the theory term in
   parentheses plus the melody relation; Detailed adds one guide-tone note. Never more than 3
   sentences, no em dashes.

## Tension metric

`chordTension` (0..100): quality base (major 4, minor 8, maj7 18, m9 24, 7 32, 9 38, ø7 40,
°7 52, ...) + 10 per tone outside the key (4 for the minor-key leading tone) + 8 for a chromatic
root + 6 for a foreign slash bass. `Suggestion::tension` is its duration-weighted mean over the
whole progression.

## Measured timing

Release build (GCC, `-O3`), shared 2-core VM, F minor fixture repeated to 32 bars (40 segments),
density 5, walking bass: 12 suggestions in **68 to 83 ms** (best of 3 per run, several runs).
Target is 300 ms. The voicing cache (per chord, bass note and top target) is shared across the
suggestions of one `generate` call, which is where most of the time is saved.

## Consequences and follow-ups

- `SimpleVoicer` stays for existing callers; production voicing goes through the solver.
- The vocabulary has no 13, alt or b9 chord symbols, so colour beyond 9ths shows up only as
  melody tensions. Extending `ChordQuality` would let the Extension family go further.
- Fewer than 6 suggestions can come back when the enabled families have almost nothing to work
  with (for example only Tritone subs on a loop without dominants).
