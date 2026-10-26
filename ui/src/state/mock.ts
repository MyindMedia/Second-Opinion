// Realistic mock data so the UI runs in a normal browser (and in the plugin until analysis lands).
// 32 bars of a vocal topline in F minor at 92 BPM, as in the design brief.

import type { AnalysisResult, ChordSegment, HarmonicFunction, MelodyNote, Suggestion, SuggestionChord, Technique, Why } from './types';

type Row = [symbol: string, beats: number, roman: string, fn: HarmonicFunction, why?: Why];

const ORIGINAL: Row[] = [
  ['Fm9', 4, 'i9', 'tonic'],
  ['D♭maj7', 4, 'VImaj7', 'predominant'],
  ['A♭/C', 4, 'III', 'tonic'],
  ['E♭sus4', 2, 'VIIsus4', 'dominant'],
  ['E♭', 2, 'VII', 'dominant'],
  ['Fm9', 4, 'i9', 'tonic'],
  ['D♭maj7', 4, 'VImaj7', 'predominant'],
  ['B♭m7', 4, 'iv7', 'predominant'],
  ['C7sus4', 2, 'V7sus4', 'dominant'],
  ['C7', 2, 'V7', 'dominant'],
];

const ALTS: Record<string, string[]> = {
  'Fm9': ['Fm7', 'A♭maj7/F', 'Fm'],
  'D♭maj7': ['Fm/D♭', 'D♭', 'D♭maj9'],
  'A♭/C': ['Cm7♭5', 'Cm', 'A♭add9/C'],
  'E♭sus4': ['A♭/E♭', 'E♭7sus4', 'B♭m/E♭'],
  'E♭': ['E♭7', 'Gm/E♭', 'E♭sus4'],
  'B♭m7': ['D♭6', 'B♭m9', 'B♭m'],
  'C7sus4': ['B♭/C', 'Fm/C', 'C7'],
  'C7': ['Edim7/C', 'C', 'C7sus4'],
};

// Bars (0-based segment index across the whole capture) the detector is unsure about.
const LOW_CONFIDENCE: Record<number, number> = { 12: 0.42, 17: 0.38, 28: 0.61, 38: 0.46 };

function phraseRepeat<T>(rows: Row[], phrases: number, make: (r: Row, start: number, index: number) => T): T[] {
  const out: T[] = [];
  let beat = 0;
  let index = 0;
  for (let p = 0; p < phrases; p++) {
    for (const r of rows) {
      out.push(make(r, beat, index++));
      beat += r[1];
    }
  }
  return out;
}

function seeded(seed: number) {
  // Deterministic mock confidences (no Math.random, per the determinism rule).
  let s = seed >>> 0;
  return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 2 ** 32);
}

export function mockAnalysis(): AnalysisResult {
  const rnd = seeded(92);
  const segments: ChordSegment[] = phraseRepeat(ORIGINAL, 4, ([symbol, beats, roman, fn], startBeat, i) => {
    const confidence = LOW_CONFIDENCE[i] ?? 0.72 + rnd() * 0.26;
    const alts = (ALTS[symbol] ?? []).map((s, k) => ({
      symbol: s,
      confidence: Math.max(0.04, (1 - confidence) * [0.55, 0.3, 0.15][k] + (LOW_CONFIDENCE[i] ? 0.05 : 0)),
    }));
    return { id: `o${i}`, startBeat, beats, symbol, confidence, roman, fn, alternatives: alts };
  });

  const phrase: [number, number, number][] = [
    [72, 0, 1.5], [68, 1.5, 1], [67, 2.5, 1.5],
    [65, 4, 2], [68, 6, 2],
    [72, 8, 1], [75, 9, 1], [72, 10, 2],
    [70, 12, 2], [73, 14, 2],
    [72, 16, 1], [68, 17, 1], [67, 18, 2],
    [65, 20, 2], [72, 22, 2],
    [72, 24, 2], [70, 26, 2],
    [67, 28, 2], [64, 30, 2],
  ];
  const melody: MelodyNote[] = [];
  for (let p = 0; p < 4; p++)
    for (const [pitch, start, beats] of phrase) melody.push({ pitch, startBeat: start + p * 32, beats });

  return {
    schemaVersion: 1,
    source: 'captured',
    sourceName: 'Lead vocal',
    bars: 32,
    bpm: 92,
    beatsPerBar: 4,
    key: 'F minor',
    keyConfidence: 0.86,
    segments,
    melody,
  };
}

interface SuggestionDef {
  id: string;
  title: string;
  techniques: Technique[];
  tension: number;
  movement: number;
  summary: string;
  phrase: Row[];
}

const w = (plain: string, term: string, detail?: string): Why => ({ plain, term, detail });

const DEFS: SuggestionDef[] = [
  {
    id: 's-dorian', title: 'Dorian lift in bar 7', techniques: ['Borrowed'], tension: 34, movement: 2.1,
    summary: 'Keeps your changes and brightens the climb back to C7.',
    phrase: [
      ...ORIGINAL.slice(0, 7),
      ['B♭9', 4, 'IV9', 'predominant', w('A major IV instead of the minor iv brightens the climb back to C7 without leaving F minor.', 'modal interchange from F Dorian, IV9', 'A♭ falls a half step to G and D steps down to C, while F and C hold.')],
      ...ORIGINAL.slice(8),
    ],
  },
  {
    id: 's-sec', title: 'Secondary dominant into iv', techniques: ['Secondary'], tension: 52, movement: 2.6,
    summary: 'F7 in first inversion pulls bar 6 into B♭m.',
    phrase: [
      ...ORIGINAL.slice(0, 6),
      ['D♭maj7', 2, 'VImaj7', 'predominant'],
      ['F7/A', 2, 'V7/iv', 'dominant', w('Turns F into a dominant that points straight at B♭m, with A in the bass climbing a half step to B♭.', 'secondary dominant, V7/iv in first inversion', 'A, the 3rd of F7, resolves up to B♭; E♭, the 7th, falls to D♭.')],
      ['B♭m9', 4, 'iv9', 'predominant', w('The added 9th makes the arrival feel soft rather than final.', 'iv9')],
      ...ORIGINAL.slice(8),
    ],
  },
  {
    id: 's-tritone', title: 'Tritone sub turnaround', techniques: ['Tritone sub'], tension: 66, movement: 1.8,
    summary: 'G♭7 slides down a half step into the top of the loop.',
    phrase: [
      ...ORIGINAL.slice(0, 8),
      ['C7sus4', 2, 'V7sus4', 'dominant'],
      ['G♭7', 2, 'subV7', 'dominant', w('Swaps the second C7 for G♭7, which slides down a half step into F minor instead of jumping.', 'tritone substitution, subV7', 'G♭7 keeps C7\'s tritone (B♭ and E), and its root falls a half step to F.')],
    ],
  },
  {
    id: 's-mediant', title: 'Neapolitan side-step', techniques: ['Chromatic mediant'], tension: 72, movement: 3.4,
    summary: 'G♭maj7 replaces the plain E♭ and drops a half step home.',
    phrase: [
      ...ORIGINAL.slice(0, 3),
      ['E♭sus4', 2, 'VIIsus4', 'dominant'],
      ['G♭maj7', 2, '♭IImaj7', 'predominant', w('Lifts a minor third from E♭ to G♭ for an unexpected glow, then falls a half step into Fm9.', 'chromatic mediant of E♭, Neapolitan ♭IImaj7', 'B♭ holds from E♭sus4 while A♭ and E♭ step down to G♭ and D♭; then G♭ falls a half step to F.')],
      ...ORIGINAL.slice(5),
    ],
  },
  {
    id: 's-steps', title: 'Stepwise bass climb', techniques: ['Diatonic'], tension: 20, movement: 1.2,
    summary: 'Bass walks C, D♭, E♭, F into bar 5.',
    phrase: [
      ...ORIGINAL.slice(0, 3),
      ['B♭m7/D♭', 2, 'iv7', 'predominant', w('Puts D♭ in the bass so the line walks up C, D♭, E♭ to F.', 'iv7 in first inversion, passing bass')],
      ...ORIGINAL.slice(4),
    ],
  },
  {
    id: 's-parallel', title: 'Parallel flip to major', techniques: ['Neo-Riemannian', 'Borrowed'], tension: 58, movement: 1.5,
    summary: 'One bar of F major lights up the second half.',
    phrase: [
      ...ORIGINAL.slice(0, 6),
      ['F', 4, 'I', 'tonic', w('Flips bar 5\'s F minor to F major for one bar, a sudden warm light before B♭m.', 'Neo-Riemannian P (parallel), I borrowed from F major', 'From bar 5\'s Fm, one note does the work: A♭ rises a half step to A.')],
      ...ORIGINAL.slice(7),
    ],
  },
  {
    id: 's-gospel', title: 'Passing diminished', techniques: ['Secondary'], tension: 62, movement: 1.6,
    summary: 'D°7 links D♭ to E♭ with a chromatic bass.',
    phrase: [
      ORIGINAL[0],
      ['D♭maj7', 2, 'VImaj7', 'predominant'],
      ['D°7', 2, 'vii°7/VII', 'dominant', w('A passing diminished chord walks the bass from D♭ to E♭ by half steps.', 'leading-tone diminished seventh of E♭, vii°7/VII')],
      ['A♭/E♭', 4, 'III', 'tonic', w('Lands on A♭ over E♭ so the chromatic bass arrives on a strong beat.', 'III in second inversion')],
      ...ORIGINAL.slice(3),
    ],
  },
  {
    id: 's-ninths', title: 'Richer 9ths', techniques: ['Diatonic'], tension: 28, movement: 0.9,
    summary: 'Same changes, warmer extensions.',
    phrase: [
      ORIGINAL[0],
      ['D♭maj9', 4, 'VImaj9', 'predominant', w('Adds the 9th, E♭, for a softer, more open D♭.', 'VImaj9')],
      ...ORIGINAL.slice(2, 6),
      ['D♭maj9', 4, 'VImaj9', 'predominant', w('Adds the 9th, E♭, for a softer, more open D♭.', 'VImaj9')],
      ['B♭m9', 4, 'iv9', 'predominant', w('The 9th, C, doubles the melody and smooths the line into C7sus4.', 'iv9')],
      ...ORIGINAL.slice(8),
    ],
  },
];

export function mockSuggestions(analysis: AnalysisResult, seed = 0): Suggestion[] {
  const originalAt = new Map(analysis.segments.map((s) => [s.startBeat, s]));

  const list = DEFS.map((d) => {
    const chords: SuggestionChord[] = phraseRepeat(d.phrase, 4, ([symbol, beats, roman, fn, why], startBeat, i) => {
      const o = originalAt.get(startBeat);
      const changed = !o || o.symbol !== symbol || o.beats !== beats;
      return { id: `${d.id}-${i}`, startBeat, beats, symbol, roman, fn, changed, why };
    });
    return {
      schemaVersion: 1 as const,
      id: d.id,
      title: d.title,
      techniques: d.techniques,
      tension: d.tension,
      movement: d.movement,
      summary: d.summary,
      chords,
    };
  });

  // "Shuffle" rotates the order deterministically from the seed.
  const k = ((seed % list.length) + list.length) % list.length;
  return [...list.slice(k), ...list.slice(0, k)];
}
