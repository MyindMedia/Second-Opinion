// Bridge payload types live in ../bridge/contract.ts (schemaVersion 1). Re-exported here so UI code
// keeps one import path; UI-only types follow.

export type {
  Alternative, AnalysisResult, ChordSegment, HarmonicFunction, MelodyNote, Suggestion, SuggestionChord,
  Technique, Why, Song, SongSection, SectionOption, SongChord, Mood, IdeaSection, Structure, AiStatus,
} from '../bridge/contract';

export type Phase = 'empty' | 'armed' | 'recording' | 'analyzing' | 'ready' | 'error';

export interface ProgressStage {
  label: string;
  pct: number;
}
