import type { AiStatus, AnalysisResult, IdeaSection, Mood, Phase, ProgressStage, Song, Structure, Suggestion } from './types';
import type { AuditionTarget } from '../bridge/contract';
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
  useOnlineAi: boolean;    // Build out: read the description with the online AI (needs a key)
}

export type Mode = 'reharm' | 'build';

export interface BuildForm {
  description: string;
  moods: Mood[];
  ideaSection: IdeaSection;
  structure: Structure;
}

export interface DragPreview {
  title: string;
  startBeat: number;
  totalBeats: number;
  chords: { startBeat: number; beats: number }[];
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
  generation?: number;             // native id of the current suggestion set (undo, restoreSnapshot)
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
  errorTitle?: string;
  needsTempo: boolean;
  settingsOpen: boolean;
  settingsTab: string;
  drawerOpen: boolean;
  compact: boolean;
  settings: Settings;
  hostTempo?: number;
  hostPlaying: boolean;
  history: Suggestion[][];
  captureMaxBars: number;
  auditionTarget?: AuditionTarget;
  dragPreview?: DragPreview;

  // Build out
  mode: Mode;
  build: BuildForm;
  buildSeed: number;
  song?: Song;
  songStatus: 'idle' | 'building' | 'error';
  songError?: string;
  songProgress: number;
  selectedSection: number;
  focusedOption?: number;          // option shown in the WhyPanel; defaults to the chosen one
  dragScope: 'section' | 'song';
  songListen: 'section' | 'song';  // what Listen plays in Build out
  songZoom: number;                // 1 = whole song fits the timeline
  aiStatus: AiStatus;
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
  useOnlineAi: false,
};

export const defaultBuildForm: BuildForm = { description: '', moods: [], ideaSection: 'verse', structure: 'auto' };
