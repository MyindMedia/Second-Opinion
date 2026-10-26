# ADR 0005: DSP-first analysis engine

Status: accepted (MVP). Date: 2026-10-26.

## Context

PRD 5.2 and 7.4 need key, chord segments (with slash bass, confidence and top 3 alternatives) and
melody from captured audio. The preferred note source is Basic Pitch (ONNX), built separately.
We need a working, deterministic, headless engine now that can take Basic Pitch notes later as an
optional input.

## Decision

`so::analysis::analyze()` in `src/core/analysis/` (no JUCE) runs three stages with progress and
cancel:

1. **Preparing.** Crop to `maxBars`, resample to 22,050 Hz with a Kaiser-windowed sinc (24 zero
   crossings, beta 8.6, cutoff at 94% of output Nyquist), peak-normalise to 0.5 (gain capped at
   +40 dB).
2. **Transcribing notes.** If `AnalyzerOptions::notes` is set (Basic Pitch), use it and take the
   skyline as melody. Otherwise a YIN tracker (1024-sample window, hop 256, FFT autocorrelation,
   threshold 0.15) gives a pitch track that is segmented on gaps, held pitch changes (hysteresis
   0.2 semitone, 3 frames) and re-attacks. Onsets and offsets are refined on a 5 ms envelope,
   notes under 80 ms are dropped. Notes go to the beat grid at 1/16 resolution.
3. **Reading harmony.** Blackman-windowed FFT frames (Fast 4096/2048, Balanced 8192/1024,
   Accurate 8192/512). First a global tuning offset is estimated (amplitude-weighted circular
   mean of the deviation of strong peaks, 80 Hz to 1.6 kHz, from the equal-tempered grid; 0 when
   the peaks disagree) and stored as `AnalysisResult::tuningCents`; chroma and the YIN tracker
   map pitches relative to it. Spectral peaks are refined by parabolic interpolation and assigned
   to MIDI 24..96; harmonic suppression walks upwards and removes 0.8 x the modelled partials of
   each note (h = 2..10). The model is 1/h, except for bass notes (below MIDI 52): their
   brightness b is measured from partials 2 to 5 (second-smallest estimate, so one real chord
   note or one phase cancellation can't skew it) and partial h is modelled as b^log2(h), with b
   between 0.5 (the 1/h model) and 0.9. A bright bass's 3rd partial (a 12th up) no longer passes
   for a real fifth. Pitches are log-compressed and folded into chroma (MIDI >= 52) and bass
   chroma (< 52). Notes, when given, are blended in per frame. Frames are averaged per beat with
   a raised-sine weight; beats 30 dB under the loudest beat count as silent.
   The decoder scores every (root, quality, bass) state, 18 x 12 x 12 = 2592, as
   `2 cos(upper chroma, template) + bass chroma[bass] - bass cost - 0.04 complexity - key penalty`.
   Bass cost: root 0, 3rd 0.15, 5th 0.2, 7th 0.3, other chord tone 0.35, foreign 0.45. So G B D
   over F reads G/F, A C E G over A reads Am7 rather than C6/A. An inverted chord whose 6th, 7th
   or 9th sits a 12th above the bass (Abmaj7/C: G over C) loses 0.5 x upper[12th] x bass[bass],
   since that tone may be the bass's own partial. Viterbi runs over beats with free
   self-transition and a change cost of 3 (downbeat), 4.5 (beat 3) or 8 (other beats) log units,
   in O(states) per beat. Silent beats score flat, so they extend the previous chord; leading
   silence stays uncovered. Confidence is the softmax (T = 0.1) posterior of the segment's mean
   score; the next three states are the alternatives.
   Key: Krumhansl-Kessler correlation over a histogram of beat chroma plus decoded chord tones
   (first and last chord weighted 1.5x). Two passes: chroma-only key as a decoding prior, then a
   chord-aware key; chords are re-decoded only if it moves. The second pass adds tonal-centre
   evidence to the profile winner and its relative only (when within 0.3 of it): root-position
   tonic first (0.05) and last (0.06), cadences into the tonic (V-i 1.0, bVII-i 0.8, vii-i 0.7,
   IV-I 0.5, iv-i and v-i 0.4, times 0.06, capped at 0.15, the loop wrap from last to first
   chord counted at 0.75), tonic chord time share x 0.08 and bass-on-tonic time share x 0.08,
   all doubled; an inverted tonic counts 0.3. Profiles can't separate relative keys (an F minor
   loop full of Ab, C and Eb correlates best with Ab major); these cues do, and limiting them to
   the relative pair stops a loop that ends on V from moving the key to the dominant.
   A `keyOverride` skips estimation.
   Every chord is spelled with `ChordNamer::respell` in the final key.

`SourceHint::Vocal` blends the tracked melody into chroma (weight 0.6), raises the key prior and
complexity penalty and slows harmonic rhythm, so a lone voice gets simple diatonic harmony.

Files without a host grid use `chooseFileGrid`. Since P2-05 the onset frames are shifted by the
half-window latency (23 ms), the bar phase weighs chroma novelty across the beat (1.5) above low
and full-band onsets (1.0), a half-beat phase that collects at least 80% of the onset energy is
decided by harmonic change, and when the host tempo is half or double the detected one the
phase is recomputed for the tempo in use.

## Evaluation (P2-05)

- **Set:** `tests/fixtures/eval/`, 14 clips (16 to 26 s, 288 s of chords, 22,050 Hz mono, plain
  binary files) synthesised by `tools/eval/make_fixtures.py` from fixed seeds, so they carry no rights
  issues: inharmonic additive piano, FM electric piano, detuned saw pad, Karplus-Strong guitar,
  formant-filtered sung melody, plain, saw and overdriven bass, light drums, small reverb. Keys
  on 12 tonics, 72 to 128 BPM, pickups up to 0.4 s, inversions and slash chords (Ab/C, Eb/G,
  Db/F, D/F#, C#m/E, a guitar Ab/C over its own low string), sus and 7sus4 chords, the STATUS.md
  F minor loop with the plugin's own voicings, relative-key pairs (F minor vs Ab major on the
  same chords, A minor vs C major), V-i and bVII-i cadences, detuning of +46 and -42 cents.
- **Labels:** PRD 8 format (`tools/labels/schema.json`), validated by the eval script.
- **Method:** `so_analyze_cli` (headless; reads the WAV, `chooseFileGrid` with the label tempo as
  the host tempo, `trimTrailingTail`, then `analyze`) prints JSON; `tools/eval/eval_chords.py`
  compares on a 10 ms grid in seconds, mir_eval-style: majmin (root + intervals below 8
  semitones, for references reducing to maj, min or N), sevenths (root + 12-semitone quality,
  references reducing to maj, min, maj7, 7, min7 or N; 9ths count as their 7th chord), bass
  (sounding bass pitch class), root, and key (exact, plus the MIREX weighted score). Duration
  weighted over all clips; uncovered time counts as N.
- **Gate:** `.github/workflows/eval.yml` builds the CLI on ubuntu-24.04 (DSP only, no ONNX) and
  fails when majmin, sevenths, bass or key drops more than 2 points below
  `tools/eval/baseline.json`.

| Eval set, Balanced | majmin | sevenths | bass | root | key |
|---|---|---|---|---|---|
| Before, detected grid (CI setting) | 95.6 | 82.1 | 96.3 | 96.2 | 92.9 (13/14) |
| **After, detected grid (CI setting)** | **99.4** | **88.2** | **99.9** | **99.9** | **100 (14/14)** |
| Before, label grid | 99.5 | 85.4 | 100 | 100 | 92.9 |
| After, label grid | 99.5 | 88.2 | 100 | 100 | 100 |
| Before, with Basic Pitch notes | 95.9 | 95.4 | 96.3 | 96.2 | 92.9 |
| After, with Basic Pitch notes | 99.4 | 99.4 | 99.9 | 99.9 | 100 |

What moved: the key miss (the F minor loop read as Ab major) was fixed by the tonal-centre
evidence. Most of the chord gain came from the downbeat fix: 10 of 14 clips had the bar start
one beat or half a beat late, which left the first beat uncovered. Sevenths gained from the bass
model and the 12th penalty. Tuning estimation reads the detuned clips at +47 and -41 cents
(truth +46 and -42), but the decoder already coped with them, so it does not move these numbers;
it removes the risk of notes splitting between pitch classes on real recordings.

Slash chords: the eval clips never reproduced the plugin's Ab/C to Cm error, so a dedicated grid
in `tests/unit/analysis_accuracy_test.cpp` (and a wider one used during development) measures it:
Ab/C, Cm and C over a C2 or C3 bass whose partials fall as rho^(h-1), rho 0.5 to 0.9, two bass
levels, 60 cases. Before: Ab/C root 16/20 (read as Cm or C four times), exact 8/20; Cm 18/20;
C 20/20. After: Ab/C root 20/20, exact 19/20; Cm 20/20; C 20/20.

## Measured (synthetic test audio, Release, shared 2-core VM)

- Am7 | Fmaj7 | C/E | G at 100 BPM: 16/16 beats correct root and family, 16/16 exact including
  the C/E bass. With a melody on top, +15 cents detune, 48 kHz: 16/16 exact at Fast, Balanced
  and Accurate (before P2-05 the G bar read Gadd9 for two beats where the melody holds A).
- Key: C major, A minor (with E7) and Eb major all correct, chords spelled Eb Ab Bb7. The
  Am7 | Fmaj7 | C/E | G loop now reads A minor (it starts on Am7 and has C only as C/E); before
  P2-05 it read C major. Both are defensible; the key override covers taste.
- Melody: 8/8 notes exact pitch, onsets within 20 ms of the truth (tending early).
- 64 s (32 bars at 120 BPM, 44.1 kHz) analysed in 1.0 to 1.9 s (target 10 s), including the
  tuning pass.

## Known weaknesses

- The eval set is synthetic and now near its ceiling for majmin, bass and key; it guards against
  regressions but can't show further gains. Real labelled stems are still needed (PRD 8, Q5),
  and real recordings (distortion, dense mixes, long reverb) will score lower.
- Bright plucked upper voices (Karplus-Strong guitar) add 7ths and 9ths (Abmaj9 for Ab):
  sevenths on the guitar clips are 12 to 75%. The measured partial model is used for bass notes
  only, because using it for every note over-subtracted real chord tones in octave and 12th
  voicings. Basic Pitch notes fix most of it (sevenths 99.4 with notes).
- The upper-register harmonic model is still 1/h; octave doublings over a loud bass lose some
  level.
- Offsets near 50 cents are ambiguous by nature (a quarter tone sharp of A is a quarter tone flat
  of Bb); the estimate picks one side.
- YIN is monophonic. On a full mix it finds only the dominant line (10 of 16 melody notes in the
  G song test); Basic Pitch notes replace it when available.
- Melody non-chord tones can add extensions (Gadd9) on the beats they sustain.
- Beats more than 30 dB under the loudest beat are treated as silent.
