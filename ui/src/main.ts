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

import { callNative, isNative, onBackendEvent, params } from './bridge/juce';
import { ControlRail } from './components/rail';
import { SuggestionGrid } from './components/cards';
import { DragStripPreview } from './components/dragstrip';
import { Header } from './components/header';
import { Banner, ChordPopover, SettingsSheet, Stage, Toast } from './components/overlays';
import { Timeline } from './components/timeline';
import { WhyPanel } from './components/why';
import { mockAnalysis, mockSuggestions } from './state/mock';
import { createStore, defaultSettings, type Settings, type State } from './state/store';
import type { AnalysisResult, ChordSegment, Phase, Suggestion } from './state/types';
import { el } from './ui/dom';

const STAGES = ['Preparing', 'Transcribing notes', 'Reading harmony', 'Writing suggestions'];

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
} satisfies State);

// ---------------------------------------------------------------------------------------------
// Undo / redo (FR-ST-02): snapshots of user edits (corrections, locks, generations)
// ---------------------------------------------------------------------------------------------
type Snap = Pick<State, 'analysis' | 'locked' | 'suggestions' | 'selectedSuggestionId'>;
const undoStack: Snap[] = [];
const redoStack: Snap[] = [];
const snap = (): Snap => {
  const s = store.get();
  return { analysis: s.analysis, locked: new Set(s.locked), suggestions: s.suggestions, selectedSuggestionId: s.selectedSuggestionId };
};
function commit(patch: Partial<State>) {
  undoStack.push(snap());
  if (undoStack.length > 50) undoStack.shift();
  redoStack.length = 0;
  store.set(patch);
}

// ---------------------------------------------------------------------------------------------
// Generation (mock until ReharmService lands): filters by Color, ranks by distance to Tension,
// keeps locked bars exactly as heard (FR-RH-03, FR-RH-02, FR-RH-08).
// ---------------------------------------------------------------------------------------------
const FAMILY_INDEX: Record<string, number> = { Diatonic: 0, Borrowed: 1, Secondary: 2, 'Tritone sub': 3, 'Chromatic mediant': 4, 'Neo-Riemannian': 5 };

function generateSet(analysis: AnalysisResult, seed: number, locked: Set<string>): Suggestion[] {
  const enabled = params.colors.map((p) => p.value);
  const target = params.tension.value;
  const lockedSegs = analysis.segments.filter((s) => locked.has(s.id));
  const isLocked = (b: number, len: number) => lockedSegs.some((s) => b < s.startBeat + s.beats && b + len > s.startBeat);

  let set = mockSuggestions(analysis, 0).filter((sg) => sg.techniques.every((t) => enabled[FAMILY_INDEX[t]] || t === 'Diatonic'));
  set.sort((a, b) => Math.abs(a.tension - target) - Math.abs(b.tension - target));
  const k = set.length ? seed % set.length : 0;
  set = [...set.slice(k), ...set.slice(0, k)];

  return set.map((sg) => {
    if (!lockedSegs.length) return sg;
    const chords = sg.chords.filter((c) => !isLocked(c.startBeat, c.beats));
    for (const seg of lockedSegs)
      chords.push({ id: `${sg.id}-L${seg.id}`, startBeat: seg.startBeat, beats: seg.beats, symbol: seg.symbol, roman: seg.roman, fn: seg.fn, changed: false });
    chords.sort((a, b) => a.startBeat - b.startBeat);
    return { ...sg, chords, containsLocked: true };
  });
}

function pushToNative(sg?: Suggestion) {
  const s = store.get();
  if (!sg || !s.analysis) return;
  void callNative('setProgression', {
    id: sg.id,
    title: sg.title,
    bpm: s.hostTempo ?? s.analysis.bpm,
    chords: sg.chords.map((c) => ({ symbol: c.symbol, beats: c.beats })),
  });
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------
let captureTimer = 0;
let analyzeTimer = 0;

function setPhase(phase: Phase) {
  store.set({ previousPhase: store.get().phase, phase });
}

function runAnalysis(sourceName = 'Lead vocal', source: 'captured' | 'file' = 'captured') {
  window.clearInterval(analyzeTimer);
  const started = performance.now();
  store.set({ progress: STAGES.map((label) => ({ label, pct: 0 })), progressStarted: started, popoverSegmentId: undefined });
  setPhase('analyzing');
  const durations = [500, 1600, 1100, 600]; // simulated; real numbers come from AnalysisService (P2-04)
  analyzeTimer = window.setInterval(() => {
    let t = performance.now() - started;
    const progress = STAGES.map((label, i) => {
      const pct = Math.max(0, Math.min(100, (t / durations[i]) * 100));
      t -= durations[i];
      return { label, pct };
    });
    store.set({ progress });
    if (progress[3].pct >= 100) {
      window.clearInterval(analyzeTimer);
      const analysis = { ...mockAnalysis(), source, sourceName };
      const suggestions = generateSet(analysis, 0, new Set());
      store.set({ analysis, suggestions, locked: new Set(), bannerDismissed: false, history: [] });
      setPhase('ready');
      selectSuggestion(suggestions[0]?.id);
    }
  }, 50);
}

function selectSuggestion(id?: string) {
  const s = store.get();
  const sg = s.suggestions.find((x) => x.id === id);
  const firstChanged = sg?.chords.find((c) => c.changed);
  store.set({ selectedSuggestionId: id, focusedChord: firstChanged ? { lane: 'suggestion', id: firstChanged.id } : undefined, abMode: 'suggestion' });
  pushToNative(sg);
  void callNative('setSessionValue', 'ui.selected', id ?? '');
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

const actions = {
  capture() {
    if (store.get().phase === 'analyzing') return;
    window.clearInterval(captureTimer);
    store.set({ recordingBar: 0, inputLevel: 0 });
    setPhase('armed');
    // Simulated host transport (real capture: P1-06). Starts "playing" after a moment.
    window.setTimeout(() => {
      if (store.get().phase !== 'armed') return;
      setPhase('recording');
      const started = performance.now();
      captureTimer = window.setInterval(() => {
        const bar = Math.min(32, Math.floor(((performance.now() - started) / 1000) * 4) + 1);
        store.set({ recordingBar: bar, inputLevel: 0.35 + Math.abs(Math.sin(performance.now() / 180)) * 0.5 });
        if (bar >= 32) { window.clearInterval(captureTimer); runAnalysis(); }
      }, 60);
    }, 1400);
  },
  stopCapture() {
    window.clearInterval(captureTimer);
    const s = store.get();
    if (s.phase === 'recording' && s.recordingBar >= 2) runAnalysis();
    else setPhase(s.analysis ? 'ready' : 'empty');
  },
  cancel() {
    window.clearInterval(analyzeTimer);
    const prev = store.get().analysis ? 'ready' : 'empty';
    setPhase(prev);
  },
  generate() {
    const s = store.get();
    if (!s.analysis || s.generating) return;
    store.set({ generating: true });
    window.setTimeout(() => {
      const st = store.get();
      const seed = st.seed;
      const suggestions = generateSet(st.analysis!, seed, st.locked);
      commit({ suggestions, generating: false, history: [...st.history, st.suggestions].slice(-20) });
      if (!suggestions.length) store.set({ toast: { kind: 'info', message: 'No suggestions with these colors. Turn on another family.' } });
      selectSuggestion(suggestions.find((x) => x.id === st.selectedSuggestionId)?.id ?? suggestions[0]?.id);
    }, 380);
  },
  shuffle() {
    store.set({ seed: store.get().seed + 1 });
    actions.generate();
  },
  audition(id?: string) {
    const s = store.get();
    if (id && id !== s.selectedSuggestionId) {
      selectSuggestion(id);
      store.set({ auditioning: true });
      return;
    }
    store.set({ auditioning: !s.auditioning });
  },
};

// Audition playhead (preview synth arrives in P3-04; in the plugin the host transport drives this).
let last = 0;
function tick(t: number) {
  const s = store.get();
  if (s.auditioning && s.analysis && !s.hostPlaying) {
    const dt = last ? (t - last) / 1000 : 0;
    const total = s.analysis.bars * s.analysis.beatsPerBar;
    const beat = ((s.playheadBeat ?? 0) + (dt * (s.hostTempo ?? s.analysis.bpm)) / 60) % total;
    store.set({ playheadBeat: beat });
    document.documentElement.style.setProperty('--mini-play', `${((beat % 16) / 16) * 100}%`);
  } else if (!s.auditioning && !s.hostPlaying && s.playheadBeat !== null) {
    store.set({ playheadBeat: null });
  }
  last = t;
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

// Host transport from the plugin (C++ → JS "transport" event)
onBackendEvent('transport', (p: { playing: boolean; bpm: number; ppq: number; hasTempo: boolean }) => {
  const s = store.get();
  const total = (s.analysis?.bars ?? 32) * 4;
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
  selectOriginal: (id) => store.set({ popoverSegmentId: store.get().popoverSegmentId === id ? undefined : id, focusedChord: { lane: 'original', id } }),
  selectSuggestionChord: (id) => store.set({ focusedChord: { lane: 'suggestion', id }, popoverSegmentId: undefined }),
  setRange: () => undefined,
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
const why = WhyPanel(store, {
  setDepth: (depth) => updateSettings({ depth }),
  audition: () => actions.audition(),
  setAB: (abMode) => store.set({ abMode }),
  closeDrawer: () => store.set({ drawerOpen: false }),
});
const scrim = el('div', { class: 'scrim' });
scrim.addEventListener('click', () => store.set({ drawerOpen: false }));
workspace.append(grid, scrim, why);

const rail = ControlRail(store, { generate: actions.generate, shuffle: actions.shuffle });
const stage = Stage(store, {
  capture: actions.capture,
  stopCapture: actions.stopCapture,
  cancel: actions.cancel,
  retry: () => runAnalysis(),
  loadDemo: () => runAnalysis('Example vocal', 'file'),
  setTempo: () => { store.set({ needsTempo: false }); runAnalysis(); },
  dropFile: (name) => runAnalysis(name, 'file'),
});

body.append(banner, timeline, workspace, rail, stage);
app.append(header, body);
if (!isNative()) app.append(DragStripPreview(store));

const popover = ChordPopover(store, {
  replace: (id, symbol) => {
    updateSegment(id, (seg) => ({ ...seg, symbol, confidence: 1, userCorrected: true, userLocked: true }));
    const locked = new Set(store.get().locked);
    locked.add(id);
    store.set({ locked, popoverSegmentId: undefined, toast: { kind: 'info', message: `Set to ${symbol} and locked. Suggestions keep it.` } });
  },
  toggleLock: (id) => {
    const locked = new Set(store.get().locked);
    locked.has(id) ? locked.delete(id) : locked.add(id);
    commit({ locked });
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
  void callNative('setSessionValue', 'ui.settings', JSON.stringify(settings));
}

const settings = SettingsSheet(store, updateSettings, () => store.set({ settingsOpen: false }), () => store.set({ toast: { kind: 'info', message: 'Captured audio cleared from this computer.' } }));
const toast = Toast(store, () => runAnalysis());
app.append(popover, settings, toast);

// Close the popover on outside clicks.
document.addEventListener('pointerdown', (e) => {
  const t = e.target as HTMLElement;
  if (store.get().popoverSegmentId && !t.closest('.popover') && !t.closest('[data-lane=original]')) store.set({ popoverSegmentId: undefined });
});

// ---------------------------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------------------------
function undo() {
  const prev = undoStack.pop();
  if (!prev) return;
  redoStack.push(snap());
  store.set(prev);
}
function redo() {
  const next = redoStack.pop();
  if (!next) return;
  undoStack.push(snap());
  store.set(next);
}

// ---------------------------------------------------------------------------------------------
// Keyboard (FR-UX-02): Space audition, ←/→ cards, L lock, R regenerate, Cmd/Ctrl+Z undo
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
    locked.has(s.focusedChord.id) ? locked.delete(s.focusedChord.id) : locked.add(s.focusedChord.id);
    commit({ locked });
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
  if (changed.has('settings')) {
    document.documentElement.style.setProperty('zoom', String(s.settings.scale / 100));
    app.classList.toggle('reduce-motion', s.settings.reduceMotion);
    applyLayout();
  }
  if (changed.has('focusedChord') && s.compact && s.focusedChord && !s.drawerOpen && s.focusedChord.lane === 'suggestion') {
    // In compact mode the drawer opens on demand only; a dot on the panel button shows there is something to read.
    header.querySelector('[data-act=drawer]')?.classList.add('has-news');
  }
  if (changed.has('drawerOpen') && s.drawerOpen) header.querySelector('[data-act=drawer]')?.classList.remove('has-news');
});

// ---------------------------------------------------------------------------------------------
// Boot: restore the session from the plugin (FR-ST-01), or honour ?state= for previews
// ---------------------------------------------------------------------------------------------
async function boot() {
  applyLayout();
  const session = await callNative<Record<string, string>>('getSession');
  if (session?.ui_settings) {
    try { store.set({ settings: { ...defaultSettings, ...JSON.parse(session.ui_settings) } }); } catch { /* ignore a bad blob */ }
  }

  const want = new URLSearchParams(location.search).get('state') ?? (session?.ui_selected ? 'ready' : 'empty');
  const ready = (selected?: string) => {
    const analysis = mockAnalysis();
    const suggestions = generateSet(analysis, 0, new Set());
    store.set({ analysis, suggestions, phase: 'ready' });
    selectSuggestion(selected && suggestions.some((x) => x.id === selected) ? selected : suggestions[0]?.id);
  };

  switch (want) {
    case 'ready': ready(session?.ui_selected); break;
    case 'popover': ready(); setTimeout(() => store.set({ popoverSegmentId: 'o12', focusedChord: { lane: 'original', id: 'o12' } }), 60); break;
    case 'analyzing': runAnalysis(); break;
    case 'armed': setPhase('armed'); break;
    case 'recording': store.set({ phase: 'recording', recordingBar: 13, inputLevel: 0.7 }); break;
    case 'error': store.set({ phase: 'error', errorMessage: 'Try soloing the vocal, or capture a cleaner section.' }); break;
    case 'tempo': store.set({ phase: 'error', needsTempo: true }); break;
    case 'settings': ready(); store.set({ settingsOpen: true, settingsTab: new URLSearchParams(location.search).get('tab') ?? 'Notation' }); break;
    default: store.set({ phase: 'empty' });
  }

  // Browser preview only: simulate host automation on Tension so the "A" badge can be reviewed.
  if (new URLSearchParams(location.search).has('automate')) {
    let t = 0;
    setInterval(() => params.tension.simulateHost(50 + Math.sin((t += 0.15)) * 30), 120);
  }
}
void boot();
