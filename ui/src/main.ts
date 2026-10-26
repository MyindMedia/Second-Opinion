import '@fontsource/ibm-plex-sans/latin-400.css';
import '@fontsource/ibm-plex-sans/latin-500.css';
import '@fontsource/ibm-plex-sans/latin-600.css';
import '@fontsource/ibm-plex-sans/greek-400.css';
import '@fontsource/ibm-plex-sans/greek-600.css';
import '@fontsource/ibm-plex-mono/latin-400.css';
import '@fontsource/ibm-plex-mono/latin-500.css';
import '@fontsource/ibm-plex-sans-condensed/latin-500.css';
import '@fontsource/ibm-plex-sans-condensed/latin-600.css';
import './styles/tokens.css';
import './styles/app.css';

import { decodeToMono } from './audio/decode';
import { encodePcmBase64, isAudioFileName } from './audio/pcm';
import { createHybridBackend, createNativeBackend, type Backend } from './bridge/backend';
import { isAnalysisResult, isSong, isSuggestionsReady, SCHEMA_VERSION, type AuditionTarget, type BackendEvents, type ProgressStageId } from './bridge/contract';
import { callNative, isNative, params, registeredFunctions } from './bridge/juce';
import { createMockBackend } from './bridge/mock-backend';
import { SuggestionGrid } from './components/cards';
import { DescriptionPanel } from './components/describe';
import { DragStripPreview } from './components/dragstrip';
import { Header } from './components/header';
import { Banner, ChordPopover, SettingsSheet, Stage, Toast } from './components/overlays';
import { ControlRail } from './components/rail';
import { SongWorkspace } from './components/sections';
import { Timeline } from './components/timeline';
import { WhyPanel } from './components/why';
import { mockAnalysis } from './state/mock';
import { arrangementChords, defaultBuildRequest, mockSong } from './state/song';
import { createStore, defaultBuildForm, defaultSettings, type BuildForm, type Mode, type Settings, type State } from './state/store';
import type { ChordSegment, Phase } from './state/types';
import { generateSet } from './bridge/mock-backend';
import { el, icon } from './ui/dom';

const STAGES = ['Preparing', 'Transcribing notes', 'Reading harmony', 'Writing suggestions'];
const STAGE_LABEL: Record<Exclude<ProgressStageId, 'song'>, string> = {
  preparing: 'Preparing', transcribing: 'Transcribing notes', harmony: 'Reading harmony', suggestions: 'Writing suggestions',
};
const DECODE = 'Decoding file';

const store = createStore({
  phase: 'empty',
  previousPhase: 'empty',
  recordingBar: 0,
  inputLevel: 0,
  progress: STAGES.map((label) => ({ label, pct: 0 })),
  progressStarted: 0,
  suggestions: [],
  seed: 0,
  generating: false,
  favorites: new Set(),
  locked: new Set(),
  auditioning: false,
  abMode: 'suggestion',
  playheadBeat: null,
  pxPerBeat: 21,
  bannerDismissed: false,
  needsTempo: false,
  settingsOpen: false,
  settingsTab: 'Notation',
  drawerOpen: false,
  compact: false,
  settings: { ...defaultSettings },
  hostPlaying: false,
  history: [],
  captureMaxBars: 32,
  mode: 'reharm',
  build: { ...defaultBuildForm },
  buildSeed: 0,
  songStatus: 'idle',
  songProgress: 0,
  selectedSection: 0,
  dragScope: 'section',
  songListen: 'section',
  songZoom: 1,
  aiStatus: { hasKey: false, maskedKey: '' },
} satisfies State);

// ---------------------------------------------------------------------------------------------
// Backend: native functions where the plugin registered them, the in-UI mock everywhere else
// ---------------------------------------------------------------------------------------------
const mock = createMockBackend({ controls: () => ({ tension: params.tension.value, colors: params.colors.map((p) => p.value) }) });
const backend: Backend = createHybridBackend(
  isNative() ? createNativeBackend((name, ...args) => callNative(name, ...args), registeredFunctions) : undefined,
  mock,
  registeredFunctions,
);

// ---------------------------------------------------------------------------------------------
// Undo / redo (FR-ST-02): snapshots of user edits (corrections, locks, generations, song choices)
// ---------------------------------------------------------------------------------------------
type Snap = Pick<State, 'analysis' | 'locked' | 'suggestions' | 'selectedSuggestionId' | 'song'>;
const undoStack: Snap[] = [];
const redoStack: Snap[] = [];
const snap = (): Snap => {
  const s = store.get();
  return { analysis: s.analysis, locked: new Set(s.locked), suggestions: s.suggestions, selectedSuggestionId: s.selectedSuggestionId, song: s.song };
};
function commit(patch: Partial<State>) {
  undoStack.push(snap());
  if (undoStack.length > 50) undoStack.shift();
  redoStack.length = 0;
  store.set(patch);
}

/** Keeps the backend's locks and song choices equal to the UI's after undo/redo. */
function syncBackend(before: Snap, after: Snap) {
  const off = [...before.locked].filter((id) => !after.locked.has(id));
  const on = [...after.locked].filter((id) => !before.locked.has(id));
  if (off.length) void backend.call('lock', off, false);
  if (on.length) void backend.call('lock', on, true);
  after.song?.sections.forEach((sec, i) => {
    if (before.song?.sections[i]?.chosen !== sec.chosen) void backend.call('chooseSectionOption', i, sec.chosen);
  });
}

// ---------------------------------------------------------------------------------------------
// Drag strip content (setProgression): the selected suggestion, or the chosen arrangement
// ---------------------------------------------------------------------------------------------
function pushToNative() {
  const s = store.get();
  if (s.mode === 'build') {
    const song = s.song;
    if (!song) { store.set({ dragPreview: undefined }); return; }
    const scope = s.dragScope;
    const chords = arrangementChords(song, scope, s.selectedSection);
    if (!chords.length) return;
    const title = scope === 'song' ? 'Whole song' : song.sections[s.selectedSection]?.label ?? 'Section';
    store.set({ dragPreview: { title, startBeat: chords[0].startBeat, totalBeats: chords.reduce((n, c) => n + c.beats, 0), chords } });
    void backend.call('setProgression', {
      id: `${song.id}:${scope === 'song' ? 'song' : `section-${s.selectedSection}`}`,
      title,
      bpm: s.hostTempo ?? song.bpm,
      chords: chords.map((c) => ({ symbol: c.symbol, beats: c.beats })),
    });
    return;
  }
  const sg = s.suggestions.find((x) => x.id === s.selectedSuggestionId);
  if (!sg || !s.analysis) { store.set({ dragPreview: undefined }); return; }
  store.set({ dragPreview: { title: sg.title, startBeat: 0, totalBeats: s.analysis.bars * s.analysis.beatsPerBar, chords: sg.chords } });
  void backend.call('setProgression', {
    id: sg.id,
    title: sg.title,
    bpm: s.hostTempo ?? s.analysis.bpm,
    chords: sg.chords.map((c) => ({ symbol: c.symbol, beats: c.beats })),
  });
}

// ---------------------------------------------------------------------------------------------
// Phase helpers
// ---------------------------------------------------------------------------------------------
function setPhase(phase: Phase) {
  if (store.get().phase !== phase) store.set({ previousPhase: store.get().phase, phase });
}

function startProgress(withDecode = false) {
  store.set({
    progress: [...(withDecode ? [DECODE] : []), ...STAGES].map((label) => ({ label, pct: 0 })),
    progressStarted: performance.now(),
    popoverSegmentId: undefined,
    errorTitle: undefined,
    errorMessage: undefined,
    needsTempo: false,
  });
  setPhase('analyzing');
}

function setStagePct(label: string, pct: number) {
  const s = store.get();
  const i = s.progress.findIndex((p) => p.label === label);
  if (i < 0) return;
  store.set({ progress: s.progress.map((p, k) => (k < i ? { ...p, pct: 100 } : k === i ? { ...p, pct } : p)) });
}

function fail(title: string, message: string) {
  store.set({ errorTitle: title, errorMessage: message, needsTempo: false });
  setPhase('error');
}

function selectSuggestion(id?: string) {
  const s = store.get();
  const sg = s.suggestions.find((x) => x.id === id);
  const firstChanged = sg?.chords.find((c) => c.changed);
  store.set({ selectedSuggestionId: id, focusedChord: firstChanged ? { lane: 'suggestion', id: firstChanged.id } : undefined, abMode: 'suggestion' });
  if (s.auditioning && s.mode === 'reharm' && id) void playReharm(true, id);
  pushToNative();
  void backend.call('setSessionValue', 'ui.selected', id ?? '');
}

function updateSegment(id: string, fn: (seg: ChordSegment) => ChordSegment | ChordSegment[] | null) {
  const a = store.get().analysis;
  if (!a) return;
  const segments: ChordSegment[] = [];
  for (const seg of a.segments) {
    const r = seg.id === id ? fn(seg) : seg;
    if (Array.isArray(r)) segments.push(...r);
    else if (r) segments.push(r);
  }
  commit({ analysis: { ...a, segments } });
}

// ---------------------------------------------------------------------------------------------
// Audition (audition() + auditionState events drive the playhead)
// ---------------------------------------------------------------------------------------------
async function playReharm(play: boolean, id = store.get().selectedSuggestionId) {
  const s = store.get();
  const target: AuditionTarget = s.abMode === 'original' ? 'original' : 'suggestion';
  store.set({ auditionTarget: target, auditioning: play });
  await backend.call('audition', { target, id: target === 'suggestion' ? id : undefined, play });
}

async function playSong(play: boolean, optionIndex?: number) {
  const s = store.get();
  if (!s.song) return;
  const target: AuditionTarget = s.songListen === 'song' ? 'song' : 'section';
  const oi = optionIndex ?? s.focusedOption;
  const sec = s.song.sections[s.selectedSection];
  const id = target === 'section' ? (oi !== undefined && oi !== sec?.chosen ? `${s.selectedSection}.${oi}` : String(s.selectedSection)) : undefined;
  store.set({ auditionTarget: target, auditioning: play });
  await backend.call('audition', { target, id, play });
}

function stopAudition() {
  const s = store.get();
  if (!s.auditioning) return;
  store.set({ auditioning: false, playheadBeat: null });
  void backend.call('audition', { target: s.auditionTarget ?? 'suggestion', play: false });
}

// ---------------------------------------------------------------------------------------------
// Files: native chooser in the plugin; in a browser a file input, decoded here
// ---------------------------------------------------------------------------------------------
let lastFile: File | undefined;
let decodeToken = 0;
const fileInput = el('input', { type: 'file', accept: 'audio/*,.wav,.aif,.aiff,.flac,.mp3,.m4a', hidden: true, 'aria-hidden': 'true', tabindex: -1 });
fileInput.addEventListener('change', () => {
  const f = fileInput.files?.[0];
  fileInput.value = '';
  if (f) void analyzeFile(f);
});

async function analyzeFile(file: File) {
  if (!isAudioFileName(file.name) && !file.type.startsWith('audio/')) {
    store.set({ toast: { kind: 'error', message: `${file.name} is not an audio file. Use WAV, AIFF, FLAC or MP3.` } });
    return;
  }
  stopAudition();
  lastFile = file;
  const token = ++decodeToken;
  startProgress(true);
  try {
    const pcm = await decodeToMono(file, (pct) => { if (token === decodeToken) setStagePct(DECODE, pct); });
    if (token !== decodeToken) return;
    if (pcm.seconds < 1) { fail('That file is too short', 'Use at least a few bars of audio.'); return; }
    const samplesBase64 = encodePcmBase64(pcm.samples);
    if (token !== decodeToken) return;
    setStagePct(DECODE, 100);
    if (pcm.truncated) store.set({ toast: { kind: 'info', message: 'Only the first 3 minutes are analyzed.' } });
    const ack = await backend.call('analyzePcm', { schemaVersion: SCHEMA_VERSION, sampleRate: pcm.sampleRate, samplesBase64, name: file.name });
    if (token === decodeToken && ack && !ack.ok && store.get().phase === 'analyzing') fail("Couldn't analyze that file", ack.error ?? 'Try another file.');
  } catch {
    if (token === decodeToken) fail("Couldn't read that file", 'It may be damaged or in a format this computer cannot decode. Try WAV, AIFF, FLAC or MP3.');
  }
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------
const actions = {
  capture() {
    const s = store.get();
    if (s.phase === 'analyzing') return;
    stopAudition();
    lastFile = undefined;
    store.set({ recordingBar: 0, inputLevel: 0 });
    setPhase('armed');
    void backend.call('startCapture').then((ack) => { if (ack && !ack.ok) store.set({ toast: { kind: 'error', message: ack.error ?? 'Capture could not start.' } }); });
  },
  stopCapture() {
    void backend.call('stopCapture');
  },
  async openFile() {
    if (backend.native && registeredFunctions().includes('openAudioFile')) {
      stopAudition();
      const r = await backend.call('openAudioFile');
      if (r && !r.ok && !r.cancelled) store.set({ toast: { kind: 'error', message: r.error ?? "Couldn't open that file." } });
      else if (r?.ok) { lastFile = undefined; startProgress(); }
      return;
    }
    fileInput.click();
  },
  cancel() {
    decodeToken++;
    void backend.call('cancel');
    setPhase(store.get().analysis ? 'ready' : 'empty');
  },
  retry() {
    if (lastFile) void analyzeFile(lastFile);
    else actions.capture();
  },
  loadDemo() {
    // The example session always comes from the mock, inside the plugin too.
    lastFile = undefined;
    startProgress();
    void mock.call('openAudioFile');
  },
  setTempo(bpm: number) {
    store.set({ needsTempo: false, hostTempo: bpm });
    startProgress();
    void backend.call('setTempo', bpm);
  },
  generate() {
    const s = store.get();
    if (!s.analysis || s.generating) return;
    store.set({ generating: true });
    void backend.call('generate', { seed: s.seed }).then((ack) => {
      if (ack && !ack.ok) store.set({ generating: false, toast: { kind: 'error', message: ack.error ?? 'Generation failed.' } });
    });
  },
  shuffle() {
    store.set({ seed: store.get().seed + 1 });
    actions.generate();
  },
  audition(id?: string) {
    const s = store.get();
    if (id && id !== s.selectedSuggestionId) {
      selectSuggestion(id);
      void playReharm(true, id);
      return;
    }
    void playReharm(!s.auditioning, id);
  },

  // Build out ------------------------------------------------------------------------------
  setMode(mode: Mode) {
    stopAudition();
    store.set({ mode, drawerOpen: false, focusedOption: undefined });
    pushToNative();
    void backend.call('setSessionValue', 'ui.mode', mode);
  },
  updateForm(patch: Partial<BuildForm>) {
    const build = { ...store.get().build, ...patch };
    store.set({ build });
    void backend.call('setSessionValue', 'ui.build', JSON.stringify(build));
  },
  async buildSong() {
    const s = store.get();
    if (!s.analysis || s.songStatus === 'building') return;
    stopAudition();
    store.set({ songStatus: 'building', songProgress: 0, songError: undefined });
    const ack = await backend.call('buildSong', {
      schemaVersion: SCHEMA_VERSION,
      description: s.build.description.trim(),
      moods: s.build.moods,
      ideaSection: s.build.ideaSection,
      structure: s.build.structure,
      seed: s.buildSeed,
      useOnlineAi: s.settings.useOnlineAi && s.aiStatus.hasKey,
    });
    if (ack && !ack.ok) store.set({ songStatus: 'error', songError: ack.error ?? 'The song builder is busy. Try again in a moment.' });
  },
  shuffleSong() {
    if (!store.get().song) return;
    store.set({ buildSeed: store.get().buildSeed + 1 });
    void actions.buildSong();
  },
  cancelSong() {
    void backend.call('cancel');
    store.set({ songStatus: 'idle', songProgress: 0 });
  },
  selectSection(i: number) {
    const s = store.get();
    if (!s.song || i < 0 || i >= s.song.sections.length) return;
    store.set({ selectedSection: i, focusedOption: undefined });
    if (s.dragScope === 'section') pushToNative();
    if (s.auditioning && s.auditionTarget === 'section') void playSong(true);
    void backend.call('setSessionValue', 'ui.section', String(i));
  },
  useOption(j: number) {
    const s = store.get();
    const song = s.song;
    if (!song) return;
    const i = s.selectedSection;
    const sections = song.sections.map((sec, k) => (k === i ? { ...sec, chosen: j } : sec));
    commit({ song: { ...song, sections }, focusedOption: j });
    void backend.call('chooseSectionOption', i, j);
    pushToNative();
    if (s.auditioning) void playSong(true, j);
  },
  setScope(scope: 'section' | 'song') {
    store.set({ dragScope: scope });
    pushToNative();
  },
  exportMidi() {
    const s = store.get();
    void backend.call('exportMidi', s.mode === 'build' ? s.dragScope : 'current').then((ack) => {
      if (ack && !ack.ok) store.set({ toast: { kind: 'error', message: ack.error ?? 'Export failed.' } });
    });
  },
  setOnlineAi(on: boolean) {
    if (on && !store.get().aiStatus.hasKey) return;
    updateSettings({ useOnlineAi: on });
  },
  async saveAiKey(key: string) {
    const st = await backend.call('setAiKey', key);
    if (!st) return;
    store.set({ aiStatus: st });
    if (st.hasKey && !st.lastError) {
      updateSettings({ useOnlineAi: true });
      store.set({ toast: { kind: 'info', message: 'Key saved. Build out can now read descriptions online.' } });
    }
  },
  async removeAiKey() {
    const st = await backend.call('setAiKey', '');
    if (st) store.set({ aiStatus: st });
    updateSettings({ useOnlineAi: false });
  },
};

// ---------------------------------------------------------------------------------------------
// Events from the backend (C++ or mock)
// ---------------------------------------------------------------------------------------------
backend.on('captureState', (p: BackendEvents['captureState']) => {
  const s = store.get();
  if (p.maxBars && p.maxBars !== s.captureMaxBars) store.set({ captureMaxBars: p.maxBars });
  if (p.needsTempo) { store.set({ needsTempo: true, errorTitle: undefined }); setPhase('error'); return; }
  if (p.state === 'armed') setPhase('armed');
  else if (p.state === 'recording') { store.set({ recordingBar: p.bar, inputLevel: p.level }); setPhase('recording'); }
  else if (p.state === 'done') startProgress();
  else if (p.state === 'idle' && (s.phase === 'armed' || s.phase === 'recording')) setPhase(s.analysis ? 'ready' : 'empty');
});

backend.on('progress', (p: BackendEvents['progress']) => {
  if (p.stage === 'song') {
    if (store.get().songStatus === 'building') store.set({ songProgress: p.pct });
    return;
  }
  if (store.get().phase !== 'analyzing') startProgress();
  setStagePct(STAGE_LABEL[p.stage], p.pct);
});

backend.on('analysisReady', (a) => {
  if (!isAnalysisResult(a)) { fail("Couldn't read the analysis", 'The plugin sent an analysis this view does not understand. Update the plugin and try again.'); return; }
  mock.prime({ analysis: a });
  const locked = new Set(a.segments.filter((x) => x.userLocked).map((x) => x.id));
  undoStack.length = 0;
  redoStack.length = 0;
  store.set({ analysis: a, locked, bannerDismissed: false, history: [], keyOverride: undefined, song: undefined, songStatus: 'idle', selectedSection: 0, focusedOption: undefined });
  setPhase('ready');
});

backend.on('suggestionsReady', (p) => {
  if (!isSuggestionsReady(p)) return;
  const s = store.get();
  const fromGenerate = s.generating;
  const patch: Partial<State> = { suggestions: p.suggestions, generating: false };
  if (fromGenerate) commit({ ...patch, history: [...s.history, s.suggestions].slice(-20) });
  else store.set(patch);
  if (!p.suggestions.length) store.set({ toast: { kind: 'info', message: 'No suggestions with these colors. Turn on another family.' } });
  const keep = p.suggestions.find((x) => x.id === s.selectedSuggestionId)?.id;
  selectSuggestion(keep ?? p.suggestions[0]?.id);
});

backend.on('songReady', (song) => {
  if (store.get().songStatus !== 'building') return;   // cancelled meanwhile
  if (!isSong(song)) { store.set({ songStatus: 'error', songError: 'The song builder sent a song this view does not understand.' }); return; }
  mock.prime({ song });
  const idea = song.sections.findIndex((x) => x.isIdea);
  const first = idea >= 0 && idea + 1 < song.sections.length ? idea + 1 : 0;
  commit({ song, songStatus: 'idle', songProgress: 100, selectedSection: first, focusedOption: undefined, songZoom: 1 });
  pushToNative();
});

backend.on('auditionState', (p) => {
  const s = store.get();
  store.set({ auditioning: p.playing, playheadBeat: p.playing ? p.beat : s.hostPlaying ? s.playheadBeat : null, ...(p.target ? { auditionTarget: p.target } : {}) });
  if (p.playing) {
    const loop = s.mode === 'build' && s.song ? s.song.sections[s.selectedSection] : undefined;
    const rel = loop ? (p.beat - loop.startBar * 4) / (Math.min(loop.bars, 8) * 4) : (p.beat % 16) / 16;
    document.documentElement.style.setProperty('--mini-play', `${Math.max(0, Math.min(1, rel % 1)) * 100}%`);
  }
});

backend.on('error', (p) => {
  const s = store.get();
  switch (p.code) {
    case 'no_tempo': store.set({ needsTempo: true }); setPhase('error'); break;
    case 'song_failed': store.set({ songStatus: 'error', songError: p.message }); break;
    case 'ai_unavailable': case 'ai_key_invalid': store.set({ toast: { kind: 'info', message: p.message } }); break;
    case 'generate_failed': store.set({ generating: false, toast: { kind: 'error', message: p.message } }); break;
    case 'too_long': fail('That file is too long', p.message); break;
    case 'decode_failed': fail("Couldn't read that file", p.message); break;
    case 'no_pitch': fail("Couldn't find a steady pitch", p.message); break;
    case 'capture_failed': fail("Capture didn't start", p.message); break;
    case 'analysis_failed': fail("Couldn't analyze this audio", p.message); break;
    default:
      if (s.phase === 'analyzing') fail('Something went wrong', p.message);
      else store.set({ toast: { kind: 'error', message: p.message } });
  }
});

// Host transport from the plugin (C++ → JS "transport" event)
backend.on('transport', (p) => {
  const s = store.get();
  const total = s.mode === 'build' && s.song ? s.song.sections.reduce((n, x) => n + x.bars, 0) * 4 : (s.analysis?.bars ?? 32) * 4;
  store.set({
    hostTempo: p.hasTempo ? p.bpm : undefined,
    hostPlaying: p.playing,
    playheadBeat: p.playing && s.analysis ? ((p.ppq % total) + total) % total : s.auditioning ? s.playheadBeat : null,
  });
});

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------
const app = document.getElementById('app')!;
app.className = 'app';
if (!isNative()) app.classList.add('is-browser');

const header = Header(store, {
  capture: actions.capture,
  stopCapture: actions.stopCapture,
  openFile: () => void actions.openFile(),
  setMode: actions.setMode,
  setKey: (key) => {
    commit({ keyOverride: key });
    store.set({ toast: { kind: 'info', message: `Key locked to ${key}. Chords and numerals stay as heard.` } });
  },
  openSettings: () => store.set({ settingsOpen: true }),
  toggleDrawer: () => store.set({ drawerOpen: !store.get().drawerOpen }),
  undo,
  redo,
});

const body = el('div', { class: 'body' });
const banner = Banner(store, () => {
  const low = store.get().analysis?.segments.find((x) => x.confidence < 0.5);
  if (low) {
    document.querySelector<HTMLElement>(`[data-seg="${low.id}"]`)?.scrollIntoView({ inline: 'center', block: 'nearest' });
    store.set({ popoverSegmentId: low.id, focusedChord: { lane: 'original', id: low.id } });
  }
});

const timeline = Timeline(store, {
  selectOriginal: (id) => {
    if (store.get().mode === 'build') return;
    store.set({ popoverSegmentId: store.get().popoverSegmentId === id ? undefined : id, focusedChord: { lane: 'original', id } });
  },
  selectSuggestionChord: (id) => store.set({ focusedChord: { lane: 'suggestion', id }, popoverSegmentId: undefined }),
  setRange: () => undefined,
  selectSection: actions.selectSection,
});

const workspace = el('div', { class: 'workspace' });
const grid = SuggestionGrid(store, {
  select: (id) => selectSuggestion(id),
  toggleFavorite: (id) => {
    const f = new Set(store.get().favorites);
    f.has(id) ? f.delete(id) : f.add(id);
    store.set({ favorites: f });
  },
  audition: (id) => actions.audition(id),
});
const songWs = SongWorkspace(store, {
  selectSection: actions.selectSection,
  focusOption: (j) => store.set({ focusedOption: j }),
  useOption: actions.useOption,
  audition: (j) => {
    const s = store.get();
    const same = (s.focusedOption ?? s.song?.sections[s.selectedSection]?.chosen) === j;
    store.set({ focusedOption: j, songListen: 'section' });
    void playSong(!(s.auditioning && same && s.auditionTarget === 'section'), j);
  },
  setScope: actions.setScope,
  retry: () => void actions.buildSong(),
  cancel: actions.cancelSong,
});
const why = WhyPanel(store, {
  setDepth: (depth) => updateSettings({ depth }),
  audition: () => (store.get().mode === 'build' ? void playSong(!store.get().auditioning) : actions.audition()),
  setAB: (abMode) => {
    store.set({ abMode });
    if (store.get().auditioning) void playReharm(true);
  },
  closeDrawer: () => store.set({ drawerOpen: false }),
  setListen: (songListen) => {
    store.set({ songListen });
    if (store.get().auditioning) void playSong(true);
  },
});
const scrim = el('div', { class: 'scrim' });
scrim.addEventListener('click', () => store.set({ drawerOpen: false }));
workspace.append(grid, songWs, scrim, why);

const rail = ControlRail(store, { generate: actions.generate, shuffle: actions.shuffle });
const describe = DescriptionPanel(store, {
  update: actions.updateForm,
  setOnlineAi: actions.setOnlineAi,
  build: () => void actions.buildSong(),
  shuffle: actions.shuffleSong,
  openAiSettings: () => store.set({ settingsOpen: true, settingsTab: 'AI' }),
});
const stage = Stage(store, {
  capture: actions.capture,
  stopCapture: actions.stopCapture,
  cancel: actions.cancel,
  retry: actions.retry,
  loadDemo: actions.loadDemo,
  setTempo: actions.setTempo,
  openFile: () => void actions.openFile(),
});

const dropHint = el('div', { class: 'drop-hint', hidden: true, 'aria-hidden': 'true' }, `<div>${icon.drop()}<span>Drop to analyze this file</span></div>`);
body.append(banner, timeline, workspace, rail, describe, stage, dropHint);
app.append(header, body, fileInput);
if (!isNative()) app.append(DragStripPreview(store, actions.exportMidi));

const popover = ChordPopover(store, {
  replace: async (id, symbol) => {
    const r = await backend.call('setChord', id, symbol);
    if (r && !r.ok) { store.set({ toast: { kind: 'error', message: r.error ?? `${symbol} is not a chord we know yet.` } }); return; }
    const sym = r?.symbol ?? symbol;
    updateSegment(id, (seg) => ({ ...seg, symbol: sym, confidence: 1, userCorrected: true, userLocked: true }));
    const locked = new Set(store.get().locked);
    locked.add(id);
    store.set({ locked, popoverSegmentId: undefined, toast: { kind: 'info', message: `Set to ${sym} and locked. Suggestions keep it.` } });
  },
  toggleLock: (id) => {
    const locked = new Set(store.get().locked);
    const on = !locked.has(id);
    on ? locked.add(id) : locked.delete(id);
    commit({ locked });
    void backend.call('lock', [id], on);
  },
  split: (id) => updateSegment(id, (seg) => {
    const half = Math.max(1, Math.floor(seg.beats / 2));
    return [{ ...seg, beats: half }, { ...seg, id: `${seg.id}b`, startBeat: seg.startBeat + half, beats: seg.beats - half }];
  }),
  merge: (id) => {
    const a = store.get().analysis!;
    const i = a.segments.findIndex((x) => x.id === id);
    const next = a.segments[i + 1];
    if (!next) return;
    const segments = a.segments.filter((x) => x.id !== next.id).map((x) => (x.id === id ? { ...x, beats: x.beats + next.beats } : x));
    commit({ analysis: { ...a, segments } });
  },
  close: () => store.set({ popoverSegmentId: undefined }),
});

function updateSettings(patch: Partial<Settings>) {
  const settings = { ...store.get().settings, ...patch };
  store.set({ settings });
  void backend.call('setSessionValue', 'ui.settings', JSON.stringify(settings));
}

const settings = SettingsSheet(
  store, updateSettings, () => store.set({ settingsOpen: false }),
  () => store.set({ toast: { kind: 'info', message: 'Captured audio cleared from this computer.' } }),
  { save: (k) => void actions.saveAiKey(k), remove: () => void actions.removeAiKey() },
);
const toast = Toast(store, () => actions.retry());
app.append(popover, settings, toast);

// Close the popover on outside clicks.
document.addEventListener('pointerdown', (e) => {
  const t = e.target as HTMLElement;
  if (store.get().popoverSegmentId && !t.closest('.popover') && !t.closest('[data-lane=original]')) store.set({ popoverSegmentId: undefined });
});

// Drop an audio file anywhere on the window (FR-IN-02): decode here, then analyzePcm.
let dragDepth = 0;
const hasFiles = (e: DragEvent) => Array.from(e.dataTransfer?.types ?? []).includes('Files');
window.addEventListener('dragenter', (e) => {
  if (!hasFiles(e)) return;
  dragDepth++;
  const s = store.get();
  if (s.phase === 'analyzing' || s.phase === 'armed' || s.phase === 'recording') return;
  app.classList.add('is-drop');
  dropHint.hidden = s.phase !== 'ready';
});
window.addEventListener('dragover', (e) => { if (hasFiles(e)) e.preventDefault(); });
window.addEventListener('dragleave', (e) => {
  if (!hasFiles(e)) return;
  if (--dragDepth <= 0) { dragDepth = 0; app.classList.remove('is-drop'); dropHint.hidden = true; }
});
window.addEventListener('drop', (e) => {
  if (!hasFiles(e)) return;
  e.preventDefault();
  dragDepth = 0;
  app.classList.remove('is-drop');
  dropHint.hidden = true;
  const s = store.get();
  if (s.phase === 'analyzing' || s.phase === 'armed' || s.phase === 'recording') return;
  const f = e.dataTransfer?.files?.[0];
  if (f) void analyzeFile(f);
});

// ---------------------------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------------------------
function undo() {
  const prev = undoStack.pop();
  if (!prev) return;
  const cur = snap();
  redoStack.push(cur);
  store.set(prev);
  syncBackend(cur, prev);
  pushToNative();
}
function redo() {
  const next = redoStack.pop();
  if (!next) return;
  const cur = snap();
  undoStack.push(cur);
  store.set(next);
  syncBackend(cur, next);
  pushToNative();
}

// ---------------------------------------------------------------------------------------------
// Keyboard (FR-UX-02): Space audition, ←/→ cards or sections, L lock, R regenerate, Cmd/Ctrl+Z undo
// ---------------------------------------------------------------------------------------------
document.addEventListener('keydown', (e) => {
  const t = e.target as HTMLElement;
  if (t.closest('input, select, textarea, [role=slider]')) return;
  const s = store.get();
  const mod = e.metaKey || e.ctrlKey;

  if (mod && e.key.toLowerCase() === 'z') { e.preventDefault(); (e.shiftKey ? redo : undo)(); return; }
  if (e.key === 'Escape') {
    if (s.settingsOpen) store.set({ settingsOpen: false });
    else if (s.popoverSegmentId) store.set({ popoverSegmentId: undefined });
    else if (s.drawerOpen) store.set({ drawerOpen: false });
    return;
  }
  if (s.phase !== 'ready' || mod || s.settingsOpen) return;

  if (s.mode === 'build') {
    if (e.key === ' ' && !t.closest('button') && s.song) { e.preventDefault(); void playSong(!s.auditioning); }
    if ((e.key === 'ArrowRight' || e.key === 'ArrowLeft') && s.song && !t.closest('.seg-ctl, .chips, .mood-chips')) {
      e.preventDefault();
      actions.selectSection(s.selectedSection + (e.key === 'ArrowRight' ? 1 : -1));
    }
    if (e.key.toLowerCase() === 'r' && s.song) actions.shuffleSong();
    return;
  }

  if (e.key === ' ' && !t.closest('button')) { e.preventDefault(); actions.audition(); }
  if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
    if (t.closest('.seg-ctl, .chips')) return;
    e.preventDefault();
    const i = s.suggestions.findIndex((x) => x.id === s.selectedSuggestionId);
    const n = s.suggestions.length;
    if (!n) return;
    const next = s.suggestions[(i + (e.key === 'ArrowRight' ? 1 : n - 1) + n) % n];
    selectSuggestion(next.id);
    requestAnimationFrame(() => document.querySelector<HTMLElement>(`[data-card="${next.id}"]`)?.focus());
  }
  if (e.key.toLowerCase() === 'l' && s.focusedChord?.lane === 'original') {
    const locked = new Set(s.locked);
    const id = s.focusedChord.id;
    const on = !locked.has(id);
    on ? locked.add(id) : locked.delete(id);
    commit({ locked });
    void backend.call('lock', [id], on);
  }
  if (e.key.toLowerCase() === 'r') actions.generate();
});

// ---------------------------------------------------------------------------------------------
// Responsive: compact layout under 1000 × 640 (WhyPanel becomes a drawer), UI scale, motion
// ---------------------------------------------------------------------------------------------
function applyLayout() {
  const scale = store.get().settings.scale / 100;
  const compact = window.innerWidth / scale < 1000 || window.innerHeight / scale < 640;
  if (compact !== store.get().compact) store.set({ compact });
}
window.addEventListener('resize', applyLayout);

store.subscribe((s, changed) => {
  if (changed.has('compact') || changed.has('drawerOpen')) {
    app.classList.toggle('is-compact', s.compact);
    app.classList.toggle('drawer-open', s.compact && s.drawerOpen);
  }
  if (changed.has('mode')) {
    app.classList.toggle('mode-build', s.mode === 'build');
    app.classList.toggle('mode-reharm', s.mode === 'reharm');
  }
  if (changed.has('settings')) {
    document.documentElement.style.setProperty('zoom', String(s.settings.scale / 100));
    app.classList.toggle('reduce-motion', s.settings.reduceMotion);
    applyLayout();
  }
  const news = s.mode === 'build' ? changed.has('selectedSection') || changed.has('focusedOption') : changed.has('focusedChord') && s.focusedChord?.lane === 'suggestion';
  if (news && s.compact && !s.drawerOpen) {
    // In compact mode the drawer opens on demand only; a dot on the panel button shows there is something to read.
    header.querySelector('[data-act=drawer]')?.classList.add('has-news');
  }
  if (changed.has('drawerOpen') && s.drawerOpen) header.querySelector('[data-act=drawer]')?.classList.remove('has-news');
});
app.classList.add('mode-reharm');

// ---------------------------------------------------------------------------------------------
// Boot: restore the session from the plugin (FR-ST-01), or honour ?state= for previews
// ---------------------------------------------------------------------------------------------
async function boot() {
  applyLayout();
  const session = await backend.call('getSession');
  if (session?.ui_settings) {
    try { store.set({ settings: { ...defaultSettings, ...JSON.parse(session.ui_settings) } }); } catch { /* ignore a bad blob */ }
  }
  if (session?.ui_build) {
    try { store.set({ build: { ...defaultBuildForm, ...JSON.parse(session.ui_build) } }); } catch { /* ignore */ }
  }
  const aiStatus = await backend.call('getAiStatus');
  if (aiStatus) store.set({ aiStatus });

  const q = new URLSearchParams(location.search);
  const want = q.get('state') ?? (session?.ui_selected ? 'ready' : 'empty');
  const ready = (selected?: string) => {
    const analysis = mockAnalysis();
    mock.prime({ analysis });
    const suggestions = generateSet(analysis, 0, new Set(), { tension: params.tension.value, colors: params.colors.map((p) => p.value) });
    store.set({ analysis, suggestions, phase: 'ready' });
    selectSuggestion(selected && suggestions.some((x) => x.id === selected) ? selected : suggestions[0]?.id);
    return analysis;
  };
  const withSong = (section?: number, option?: number) => {
    const analysis = ready();
    const req = defaultBuildRequest();
    const song = mockSong(req, analysis);
    mock.prime({ song });
    const idea = song.sections.findIndex((x) => x.isIdea);
    store.set({
      mode: 'build',
      build: { description: req.description, moods: req.moods, ideaSection: req.ideaSection, structure: req.structure },
      song,
      selectedSection: section ?? Math.min(song.sections.length - 1, idea + 1),
      focusedOption: option,
    });
    pushToNative();
  };
  const buildForm = () => {
    const req = defaultBuildRequest();
    store.set({ mode: 'build', build: { description: req.description, moods: req.moods, ideaSection: req.ideaSection, structure: req.structure } });
  };

  switch (want) {
    case 'ready': ready(session?.ui_selected); if (session?.ui_mode === 'build') store.set({ mode: 'build' }); break;
    case 'popover': ready(); setTimeout(() => store.set({ popoverSegmentId: 'o12', focusedChord: { lane: 'original', id: 'o12' } }), 60); break;
    case 'analyzing': actions.loadDemo(); break;
    case 'decoding':
      startProgress(true);
      setStagePct(DECODE, 65);
      break;
    case 'armed': setPhase('armed'); break;
    case 'recording': store.set({ phase: 'recording', recordingBar: 13, inputLevel: 0.7 }); break;
    case 'error': store.set({ phase: 'error', errorMessage: 'Try soloing the vocal, or capture a cleaner section.' }); break;
    case 'tempo': store.set({ phase: 'error', needsTempo: true }); break;
    case 'settings': ready(); store.set({ settingsOpen: true, settingsTab: q.get('tab') ?? 'Notation' }); break;
    case 'build': ready(); store.set({ mode: 'build' }); pushToNative(); break;
    case 'build-form': ready(); buildForm(); pushToNative(); break;
    case 'build-loading': ready(); buildForm(); store.set({ songStatus: 'building', songProgress: 45 }); pushToNative(); break;
    case 'build-error': ready(); buildForm(); store.set({ songStatus: 'error', songError: 'The song builder stopped early. Try again, or remove a few moods.' }); pushToNative(); break;
    case 'build-song': withSong(); break;
    case 'build-section': withSong(Number(q.get('section') ?? 2), Number(q.get('option') ?? 1)); break;
    case 'build-ai':
      if (q.has('key')) await actions.saveAiKey('sk-ant-preview-0000000000000000wxyz');
      withSong();
      store.set({ settingsOpen: true, settingsTab: 'AI' });
      break;
    default: store.set({ phase: 'empty' });
  }
  if (q.has('drawer')) store.set({ drawerOpen: true });

  // Browser preview only: simulate host automation on Tension so the "A" badge can be reviewed.
  if (q.has('automate')) {
    let t = 0;
    setInterval(() => params.tension.simulateHost(50 + Math.sin((t += 0.15)) * 30), 120);
  }
}
void boot();
