import type { AnalysisResult, Phase, ProgressStage, Suggestion } from './types';
import type { Notation } from '../theory/chords';

export interface Settings {
  notation: Notation;
  preferSlash: boolean;
  spelling: 'auto' | 'flats' | 'sharps';
  depth: 'simple' | 'detailed';
  reduceMotion: boolean;
  scale: number;           // 75..200 (%)
  previewSound: 'keys' | 'pad' | 'off';
  ppq: 480 | 960;
  velocity: 'flat' | 'humanized';
  quality: 'fast' | 'balanced' | 'accurate';
  sourceHint: 'auto' | 'vocal' | 'keys' | 'guitar' | 'loop';
}

export interface State {
  phase: Phase;
  previousPhase: Phase;
  recordingBar: number;
  inputLevel: number;
  progress: ProgressStage[];
  progressStarted: number;
  analysis?: AnalysisResult;
  keyOverride?: string;
  suggestions: Suggestion[];
  seed: number;
  generating: boolean;
  selectedSuggestionId?: string;
  focusedChord?: { lane: 'original' | 'suggestion'; id: string };
  popoverSegmentId?: string;
  favorites: Set<string>;
  locked: Set<string>;             // original segment ids
  auditioning: boolean;
  abMode: 'suggestion' | 'original';
  playheadBeat: number | null;
  pxPerBeat: number;
  banner?: 'low-confidence';
  bannerDismissed: boolean;
  toast?: { kind: 'error' | 'info'; message: string; action?: string };
  errorMessage?: string;
  needsTempo: boolean;
  settingsOpen: boolean;
  settingsTab: string;
  drawerOpen: boolean;
  compact: boolean;
  settings: Settings;
  hostTempo?: number;
  hostPlaying: boolean;
  history: Suggestion[][];
}

type Listener = (s: State, changed: Set<keyof State>) => void;

export function createStore(initial: State) {
  let state = initial;
  const listeners = new Set<Listener>();
  let pending = new Set<keyof State>();
  let scheduled = false;

  return {
    get: () => state,
    set(patch: Partial<State>) {
      state = { ...state, ...patch };
      for (const k of Object.keys(patch) as (keyof State)[]) pending.add(k);
      if (!scheduled) {
        scheduled = true;
        requestAnimationFrame(() => {
          scheduled = false;
          const changed = pending;
          pending = new Set();
          for (const l of listeners) l(state, changed);
        });
      }
    },
    subscribe(l: Listener) {
      listeners.add(l);
      return () => listeners.delete(l);
    },
  };
}

export type Store = ReturnType<typeof createStore>;

export const defaultSettings: Settings = {
  notation: 'pop',
  preferSlash: true,
  spelling: 'auto',
  depth: 'simple',
  reduceMotion: false,
  scale: 100,
  previewSound: 'keys',
  ppq: 960,
  velocity: 'humanized',
  quality: 'balanced',
  sourceHint: 'auto',
};
