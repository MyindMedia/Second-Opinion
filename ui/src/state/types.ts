// Bridge payload types (PRD §7.9). schemaVersion lets the C++ side and the UI evolve separately.

export type HarmonicFunction = 'tonic' | 'predominant' | 'dominant' | 'color';

export type Technique =
  | 'Diatonic'
  | 'Borrowed'
  | 'Secondary'
  | 'Tritone sub'
  | 'Chromatic mediant'
  | 'Neo-Riemannian';

export interface Alternative {
  symbol: string;
  confidence: number;
}

export interface ChordSegment {
  id: string;
  startBeat: number;
  beats: number;
  symbol: string;
  confidence: number;      // 0..1
  roman: string;
  fn: HarmonicFunction;
  alternatives: Alternative[];
  userLocked?: boolean;
  userCorrected?: boolean;
}

export interface MelodyNote {
  pitch: number;           // MIDI
  startBeat: number;
  beats: number;
}

export interface AnalysisResult {
  schemaVersion: 1;
  source: 'captured' | 'file';
  sourceName: string;
  bars: number;
  bpm: number;
  beatsPerBar: number;
  key: string;             // "F minor"
  keyConfidence: number;
  segments: ChordSegment[];
  melody: MelodyNote[];
}

export interface Why {
  plain: string;           // plain language first
  term: string;            // theory term, shown in parentheses
  detail?: string;         // Detailed mode: voice-leading note
}

export interface SuggestionChord {
  id: string;
  startBeat: number;
  beats: number;
  symbol: string;
  roman: string;
  fn: HarmonicFunction;
  changed: boolean;        // differs from the original reading
  why?: Why;
}

export interface Suggestion {
  schemaVersion: 1;
  id: string;
  title: string;
  techniques: Technique[];
  tension: number;         // 0..100
  movement: number;        // average semitones moved per chord change
  summary: string;
  chords: SuggestionChord[];
  containsLocked?: boolean;
}

export type Phase = 'empty' | 'armed' | 'recording' | 'analyzing' | 'ready' | 'error';

export interface ProgressStage {
  label: string;
  pct: number;
}
