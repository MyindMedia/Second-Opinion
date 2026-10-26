// Bridge contract between the WebView UI and the C++ plugin, schemaVersion 1.
// Human-readable version for the C++ integrator: docs/bridge-contract.md. Keep both in sync.
//
// Conventions
//   - Every native function is registered with WebBrowserComponent::Options::withNativeFunction under
//     the exact name in NATIVE_FUNCTIONS and resolves its promise with the JSON result type below.
//   - Long work (capture, analysis, generation, song building, audition) acknowledges at once with
//     an Ack and reports through events (EVENTS), emitted with emitEventIfBrowserIsVisible.
//   - All ids are strings. Beats are quarter notes; beat 0 is bar 1, beat 1 of the capture.
//   - Chord symbols are Pop spelling with real accidentals (U+266D flat, U+266F sharp), e.g. "D♭maj7/F".
//     JUCE must build them with juce::String::fromUTF8.
//   - Enumerations are lowercase ASCII ids. The UI owns the human labels.

export const SCHEMA_VERSION = 1 as const;
export type SchemaVersion = typeof SCHEMA_VERSION;

// ---------------------------------------------------------------------------------------------
// Shared analysis shapes (mirror src/core/analysis/AnalysisTypes.h)
// ---------------------------------------------------------------------------------------------

export type HarmonicFunction = 'tonic' | 'predominant' | 'dominant' | 'color';

export type Technique = 'Diatonic' | 'Borrowed' | 'Secondary' | 'Tritone sub' | 'Chromatic mediant' | 'Neo-Riemannian';

export interface Alternative {
  symbol: string;
  confidence: number;          // 0..1
}

export interface ChordSegment {
  id: string;                  // stable within one analysis (C++ int id as a decimal string is fine)
  startBeat: number;
  beats: number;
  symbol: string;              // Pop spelling
  confidence: number;          // 0..1
  roman: string;               // e.g. "VImaj7", relative to the detected key
  fn: HarmonicFunction;
  alternatives: Alternative[]; // top 3, best first, excluding `symbol`
  userLocked?: boolean;
  userCorrected?: boolean;
}

export interface MelodyNote {
  pitch: number;               // MIDI
  startBeat: number;
  beats: number;
}

export interface AnalysisResult {
  schemaVersion: SchemaVersion;
  source: 'captured' | 'file';
  sourceName: string;          // file name, or "Lead vocal" for captures
  bars: number;
  bpm: number;
  beatsPerBar: number;
  downbeatSec?: number;
  key: string;                 // "F minor", "E♭ major"
  keyConfidence: number;       // 0..1
  segments: ChordSegment[];
  melody: MelodyNote[];
}

export interface Why {
  plain: string;               // plain language first
  term: string;                // theory term, shown in parentheses
  detail?: string;             // Detailed mode: voice-leading note
}

export interface SuggestionChord {
  id: string;
  startBeat: number;
  beats: number;
  symbol: string;
  roman: string;
  fn: HarmonicFunction;
  changed: boolean;            // differs from the original reading
  why?: Why;
}

export interface Suggestion {
  schemaVersion: SchemaVersion;
  id: string;
  title: string;
  techniques: Technique[];
  tension: number;             // 0..100
  movement: number;            // average semitones moved per chord change
  summary: string;
  chords: SuggestionChord[];
  containsLocked?: boolean;
}

// ---------------------------------------------------------------------------------------------
// Build out (song builder)
// ---------------------------------------------------------------------------------------------

export const MOODS = [
  'dark', 'hopeful', 'melancholic', 'euphoric', 'dreamy', 'tense', 'romantic', 'aggressive',
  'chill', 'nostalgic', 'cinematic', 'soulful', 'mysterious', 'triumphant', 'bittersweet', 'gritty',
] as const;
export type Mood = (typeof MOODS)[number];

export const IDEA_SECTIONS = ['verse', 'chorus', 'hook', 'loop'] as const;
export type IdeaSection = (typeof IDEA_SECTIONS)[number];

export const STRUCTURES = ['auto', 'pop', 'rnb', 'hiphop-trap', 'gospel', 'edm', 'cinematic'] as const;
export type Structure = (typeof STRUCTURES)[number];

export const SECTION_KINDS = [
  'intro', 'verse', 'pre', 'chorus', 'hook', 'post', 'bridge', 'build', 'drop', 'breakdown', 'vamp', 'outro', 'loop',
] as const;
export type SectionKind = (typeof SECTION_KINDS)[number];

/** A chord inside a section option. startBeat is relative to the start of its section. */
export interface SongChord {
  startBeat: number;
  beats: number;
  symbol: string;
  roman: string;
  fn: HarmonicFunction;
}

export interface SectionOption {
  title: string;               // "Lift to the relative major"
  explanation: string;         // plain language, one or two sentences
  energy: number;              // 0..100
  tension: number;             // 0..100
  chords: SongChord[];         // beats sum to section.bars * beatsPerBar
  transition?: string;         // optional: how this option hands off into the next section
}

export interface SongSection {
  section: SectionKind;
  label: string;               // "Verse 1", "Pre-chorus", "Chorus 2"
  startBar: number;            // 0-based bar index in the song
  bars: number;
  chosen: number;              // index into options
  isIdea: boolean;             // built from the user's audio
  options: SectionOption[];    // usually 3
}

export interface Song {
  schemaVersion: SchemaVersion;
  id: string;
  key: string;
  bpm: number;
  beatsPerBar: number;
  structure: Exclude<Structure, 'auto'>;   // the structure actually used
  moods: Mood[];               // moods actually used (chips plus what the description implied)
  interpreter: 'offline' | 'online';       // who read the description
  interpretedAs?: string;      // one line, e.g. "Dark, late-night R&B with a hopeful chorus"
  arc: string;                 // one line on the energy shape of the whole song
  sections: SongSection[];
}

// ---------------------------------------------------------------------------------------------
// Requests and results of native functions
// ---------------------------------------------------------------------------------------------

export interface Ack {
  ok: boolean;
  error?: string;              // human message when !ok
}

export interface OpenFileResult extends Ack {
  cancelled?: boolean;         // the user closed the chooser
  name?: string;               // chosen file name; analysis then reports through events
}

export interface AnalyzePcmRequest {
  schemaVersion: SchemaVersion;
  sampleRate: number;          // 22050 from the UI decoder
  samplesBase64: string;       // float32 little-endian mono, base64 (RFC 4648, with padding)
  name: string;                // file name for display
}

export interface SetChordResult extends Ack {
  symbol?: string;             // normalized Pop spelling as stored
}

export interface GenerateRequest {
  seed: number;
}

export type AuditionTarget = 'original' | 'suggestion' | 'section' | 'song';

export interface AuditionRequest {
  target: AuditionTarget;
  /** 'suggestion': the suggestion id. 'section': "<sectionIndex>" plays the chosen option,
      "<sectionIndex>.<optionIndex>" previews another option without choosing it. */
  id?: string;
  play: boolean;
}

export interface BuildSongRequest {
  schemaVersion: SchemaVersion;
  description: string;         // free text, may be empty
  moods: Mood[];
  ideaSection: IdeaSection;
  structure: Structure;
  seed: number;
  useOnlineAi: boolean;        // only honoured when a key is stored; otherwise offline is used
}

export interface ProgressionPayload {
  id: string;
  title: string;
  bpm: number;
  chords: { symbol: string; beats: number }[];
}

export interface SetProgressionResult extends Ack {
  count: number;
  unparsed?: string[];
}

/** 'current' writes whatever setProgression last sent (the drag strip's content). */
export type ExportScope = 'current' | 'section' | 'song';

export interface AiStatus {
  hasKey: boolean;
  maskedKey: string;           // "sk-ant-...abcd", empty without a key
  lastError?: string;          // last failure in plain language, cleared on success
}

export interface ParseChordResult extends Ack {
  symbol?: string;
}

/** Native functions (JS → C++). Argument lists are positional, exactly as listed. */
export interface NativeFunctions {
  startCapture(): Ack;
  stopCapture(): Ack;
  setTempo(bpm: number): Ack;
  openAudioFile(): OpenFileResult;
  analyzePcm(req: AnalyzePcmRequest): Ack;
  cancel(): Ack;
  setChord(segmentId: string, symbol: string): SetChordResult;
  lock(segmentIds: string[], locked: boolean): Ack;
  generate(req: GenerateRequest): Ack;
  audition(req: AuditionRequest): Ack;
  buildSong(req: BuildSongRequest): Ack;
  chooseSectionOption(sectionIndex: number, optionIndex: number): Ack;
  setProgression(payload: ProgressionPayload): SetProgressionResult;
  exportMidi(scope: ExportScope): Ack;
  getSession(): Record<string, string>;
  setSessionValue(key: string, value: string): boolean;
  setAiKey(key: string): AiStatus;
  getAiStatus(): AiStatus;
  parseChord(symbol: string): ParseChordResult;
}

export type NativeFunctionName = keyof NativeFunctions;

export const NATIVE_FUNCTIONS: readonly NativeFunctionName[] = [
  'startCapture', 'stopCapture', 'setTempo', 'openAudioFile', 'analyzePcm', 'cancel', 'setChord', 'lock', 'generate',
  'audition', 'buildSong', 'chooseSectionOption', 'setProgression', 'exportMidi', 'getSession',
  'setSessionValue', 'setAiKey', 'getAiStatus', 'parseChord',
];

// ---------------------------------------------------------------------------------------------
// Events (C++ → JS)
// ---------------------------------------------------------------------------------------------

export type ProgressStageId = 'preparing' | 'transcribing' | 'harmony' | 'suggestions' | 'song';

export interface ProgressEvent {
  stage: ProgressStageId;
  pct: number;                 // 0..100 within the stage; stages arrive in order
}

export type CaptureStateId = 'idle' | 'armed' | 'recording' | 'done';

export interface CaptureStateEvent {
  state: CaptureStateId;
  bar: number;                 // 1-based bar being recorded, 0 when not recording
  level: number;               // input level 0..1 (peak, smoothed)
  needsTempo: boolean;         // the host gave no tempo; the UI asks for BPM
  maxBars?: number;            // capture limit, default 32
}

export interface SuggestionsReadyEvent {
  schemaVersion: SchemaVersion;
  seed: number;
  suggestions: Suggestion[];
}

export interface AuditionStateEvent {
  playing: boolean;
  beat: number;                // song beats for 'section'/'song', capture beats for 'original'/'suggestion'
  target?: AuditionTarget;
  id?: string;
}

export interface TransportEvent {
  playing: boolean;
  bpm: number;
  ppq: number;
  numerator?: number;
  denominator?: number;
  hasTempo: boolean;
}

export type ErrorCode =
  | 'capture_failed' | 'no_tempo' | 'decode_failed' | 'too_long' | 'no_pitch' | 'analysis_failed'
  | 'generate_failed' | 'song_failed' | 'ai_unavailable' | 'ai_key_invalid' | 'export_failed' | 'busy';

export interface ErrorEvent {
  code: ErrorCode;
  message: string;             // human sentence, shown as is
}

export interface BackendEvents {
  progress: ProgressEvent;
  captureState: CaptureStateEvent;
  analysisReady: AnalysisResult;
  suggestionsReady: SuggestionsReadyEvent;
  songReady: Song;
  auditionState: AuditionStateEvent;
  transport: TransportEvent;
  error: ErrorEvent;
}

export type BackendEventName = keyof BackendEvents;

export const EVENTS: readonly BackendEventName[] = [
  'progress', 'captureState', 'analysisReady', 'suggestionsReady', 'songReady', 'auditionState', 'transport', 'error',
];

// ---------------------------------------------------------------------------------------------
// Limits and runtime guards for payloads arriving from C++
// ---------------------------------------------------------------------------------------------

export const PCM_SAMPLE_RATE = 22050;
export const PCM_MAX_SECONDS = 180;

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null;

export function isAnalysisResult(v: unknown): v is AnalysisResult {
  return isObj(v) && v.schemaVersion === SCHEMA_VERSION && Array.isArray(v.segments) && Array.isArray(v.melody)
    && typeof v.key === 'string' && typeof v.bpm === 'number' && typeof v.bars === 'number';
}

export function isSuggestionsReady(v: unknown): v is SuggestionsReadyEvent {
  return isObj(v) && v.schemaVersion === SCHEMA_VERSION && Array.isArray(v.suggestions);
}

export function isSong(v: unknown): v is Song {
  return isObj(v) && v.schemaVersion === SCHEMA_VERSION && Array.isArray(v.sections) && typeof v.key === 'string'
    && v.sections.every((s) => isObj(s) && Array.isArray(s.options) && typeof s.startBar === 'number' && typeof s.bars === 'number');
}
