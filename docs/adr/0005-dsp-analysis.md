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
   Accurate 8192/512). Spectral peaks are refined by parabolic interpolation and assigned to MIDI
   24..96; harmonic suppression walks upwards and removes 0.8 x (1/h) of each note's partials
   (h = 2..8). Pitches are log-compressed and folded into chroma (MIDI >= 52) and bass chroma
   (< 52). Notes, when given, are blended in per frame. Frames are averaged per beat with a
   raised-sine weight; beats 30 dB under the loudest beat count as silent.
   The decoder scores every (root, quality, bass) state, 18 x 12 x 12 = 2592, as
   `2 cos(upper chroma, template) + bass chroma[bass] - bass cost - 0.04 complexity - key penalty`.
   Bass cost: root 0, 3rd 0.15, 5th 0.2, 7th 0.3, other chord tone 0.35, foreign 0.45. So G B D
   over F reads G/F, A C E G over A reads Am7 rather than C6/A. Viterbi runs over beats with free
   self-transition and a change cost of 3 (downbeat), 4.5 (beat 3) or 8 (other beats) log units,
   in O(states) per beat. Silent beats score flat, so they extend the previous chord; leading
   silence stays uncovered. Confidence is the softmax (T = 0.1) posterior of the segment's mean
   score; the next three states are the alternatives.
   Key: Krumhansl-Kessler correlation over a histogram of beat chroma plus decoded chord tones
   (first and last chord weighted 1.5x). Two passes: chroma-only key as a decoding prior, then a
   chord-aware key; chords are re-decoded only if it moves. A `keyOverride` skips estimation.
   Every chord is spelled with `ChordNamer::respell` in the final key.

`SourceHint::Vocal` blends the tracked melody into chroma (weight 0.6), raises the key prior and
complexity penalty and slows harmonic rhythm, so a lone voice gets simple diatonic harmony.

## Measured (synthetic test audio, Release, shared 2-core VM)

- Am7 | Fmaj7 | C/E | G at 100 BPM: 16/16 beats correct root and family, 16/16 exact including
  the C/E bass. With a melody on top, +15 cents detune, 48 kHz: 16/16 root and family at
  Fast/Balanced/Accurate, 14 to 16 of 16 exact (the G bar reads Gadd9 for two beats where the
  melody holds A).
- Key: C major, A minor (with E7) and Eb major all correct, chords spelled Eb Ab Bb7.
- Melody: 8/8 notes exact pitch, onsets within 20 ms of the truth (tending early).
- 64 s (32 bars at 120 BPM, 44.1 kHz) analysed in 1.6 to 1.9 s (target 10 s).

## Known weaknesses

- Tests use synthetic harmonic tones; real recordings (drums, distortion, reverb, inharmonic
  piano) will score lower. A labelled real-audio set is still needed (PRD 8).
- No tuning estimation: recordings more than about 30 cents off A440 will smear chroma.
- The harmonic model assumes 1/h partials; octave doublings over a loud bass lose some level.
- YIN is monophonic. On a full mix it finds only the dominant line (10 of 16 melody notes in the
  G song test); Basic Pitch notes replace it when available.
- Melody non-chord tones can add extensions (Gadd9) on the beats they sustain.
- Beats more than 30 dB under the loudest beat are treated as silent.
