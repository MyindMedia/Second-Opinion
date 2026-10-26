// Browser-preview backend: implements the bridge contract with timers and mock data, emitting the
// same events the C++ side will. No DOM access, so it runs under Vitest with fake timers.

import { base64SampleCount } from '../audio/pcm';
import { mockAnalysis, mockSuggestions } from '../state/mock';
import { mockSong, songBeats } from '../state/song';
import { formatChord, isParsed, parseChord } from '../theory/chords';
import type {
  AiStatus, AnalysisResult, BackendEventName, BackendEvents, NativeFunctionName, NativeFunctions, ProgressStageId, Song,
  Suggestion,
} from './contract';
import { PCM_MAX_SECONDS, SCHEMA_VERSION } from './contract';
import type { Backend } from './backend';

export interface MockControls {
  tension: number;        // 0..100
  colors: boolean[];      // Diatonic, Borrowed, Secondary, Tritone sub, Chromatic mediant, Neo-Riemannian
}

export interface MockOptions {
  controls?: () => MockControls;
  /** Multiplies every simulated duration (tests use small values). */
  timeScale?: number;
}

const FAMILY_INDEX: Record<string, number> = { Diatonic: 0, Borrowed: 1, Secondary: 2, 'Tritone sub': 3, 'Chromatic mediant': 4, 'Neo-Riemannian': 5 };
const ANALYSIS_STAGES: [ProgressStageId, number][] = [['preparing', 500], ['transcribing', 1600], ['harmony', 1100], ['suggestions', 600]];

/** Filters by Color, ranks by distance to Tension, keeps locked bars exactly as heard (FR-RH-02/03/08). */
export function generateSet(analysis: AnalysisResult, seed: number, locked: Set<string>, c: MockControls): Suggestion[] {
  const lockedSegs = analysis.segments.filter((s) => locked.has(s.id));
  const isLocked = (b: number, len: number) => lockedSegs.some((s) => b < s.startBeat + s.beats && b + len > s.startBeat);
  let set = mockSuggestions(analysis, 0).filter((sg) => sg.techniques.every((t) => c.colors[FAMILY_INDEX[t]] || t === 'Diatonic'));
  set.sort((a, b) => Math.abs(a.tension - c.tension) - Math.abs(b.tension - c.tension));
  const k = set.length ? ((seed % set.length) + set.length) % set.length : 0;
  set = [...set.slice(k), ...set.slice(0, k)];
  return set.map((sg) => {
    if (!lockedSegs.length) return sg;
    const chords = sg.chords.filter((ch) => !isLocked(ch.startBeat, ch.beats));
    for (const seg of lockedSegs)
      chords.push({ id: `${sg.id}-L${seg.id}`, startBeat: seg.startBeat, beats: seg.beats, symbol: seg.symbol, roman: seg.roman, fn: seg.fn, changed: false });
    chords.sort((a, b) => a.startBeat - b.startBeat);
    return { ...sg, chords, containsLocked: true };
  });
}

export const DEFAULT_CONTROLS: MockControls = { tension: 35, colors: [true, true, true, false, false, false] };

export interface MockBackend extends Backend {
  /** Seeds state for ?state= previews without running the timed flows. */
  prime(p: { analysis?: AnalysisResult; song?: Song }): void;
}

export function createMockBackend(opts: MockOptions = {}): MockBackend {
  const controls = opts.controls ?? (() => DEFAULT_CONTROLS);
  const scale = opts.timeScale ?? 1;
  const listeners = new Map<BackendEventName, Set<(p: unknown) => void>>();
  const timers = new Set<ReturnType<typeof setTimeout>>();
  let job: 'capture' | 'analysis' | 'song' | null = null;

  let analysis: AnalysisResult | undefined;
  let song: Song | undefined;
  const locked = new Set<string>();
  let ai: AiStatus = { hasKey: false, maskedKey: '' };
  let capture = { state: 'idle' as BackendEvents['captureState']['state'], bar: 0 };
  let audition: { timer?: ReturnType<typeof setInterval>; beat: number } = { beat: 0 };

  const emit = <E extends BackendEventName>(name: E, payload: BackendEvents[E]) => {
    // Deep copy: payloads cross the bridge as JSON, so the UI never shares objects with the backend.
    const p = JSON.parse(JSON.stringify(payload));
    listeners.get(name)?.forEach((fn) => fn(p));
  };
  const later = (ms: number, fn: () => void) => {
    const t = setTimeout(() => { timers.delete(t); fn(); }, ms * scale);
    timers.add(t);
  };
  const clearJobs = () => { timers.forEach((t) => clearTimeout(t)); timers.clear(); job = null; };

  function runStages(stages: [ProgressStageId, number][], done: () => void) {
    const STEP = 50;
    let i = 0, t = 0;
    const tick = () => {
      if (i >= stages.length) return done();
      const [stage, dur] = stages[i];
      t += STEP;
      const pct = Math.min(100, (t / dur) * 100);
      emit('progress', { stage, pct });
      if (pct >= 100) { i++; t = 0; }
      later(STEP, tick);
    };
    tick();
  }

  function runAnalysis(source: 'captured' | 'file', sourceName: string) {
    clearJobs();
    job = 'analysis';
    runStages(ANALYSIS_STAGES, () => {
      job = null;
      analysis = { ...mockAnalysis(), source, sourceName };
      locked.clear();
      emit('analysisReady', analysis);
      emit('suggestionsReady', { schemaVersion: SCHEMA_VERSION, seed: 0, suggestions: generateSet(analysis, 0, locked, controls()) });
    });
  }

  function setCapture(state: BackendEvents['captureState']['state'], bar: number, level = 0) {
    capture = { state, bar };
    emit('captureState', { state, bar, level, needsTempo: false, maxBars: 32 });
  }

  function stopAudition() {
    if (audition.timer) clearInterval(audition.timer);
    audition = { beat: 0 };
  }

  const ok = { ok: true };

  const impl: { [K in NativeFunctionName]: (...a: Parameters<NativeFunctions[K]>) => ReturnType<NativeFunctions[K]> } = {
    startCapture() {
      if (job === 'analysis' || job === 'song') return { ok: false, error: 'Wait for the current job to finish.' };
      clearJobs();
      job = 'capture';
      setCapture('armed', 0);
      // Simulated host transport: starts "playing" after a moment, 4 bars per second.
      later(1400, () => {
        let bar = 1;
        const step = () => {
          setCapture('recording', bar, 0.35 + Math.abs(Math.sin(bar * 1.7)) * 0.5);
          if (bar >= 32) { setCapture('done', 32); runAnalysis('captured', 'Lead vocal'); return; }
          bar++;
          later(250, step);
        };
        step();
      });
      return ok;
    },
    stopCapture() {
      if (job !== 'capture') return ok;
      const bar = capture.bar;
      clearJobs();
      if (capture.state === 'recording' && bar >= 2) { setCapture('done', bar); runAnalysis('captured', 'Lead vocal'); }
      else setCapture('idle', 0);
      return ok;
    },
    setTempo(bpm) {
      if (!(bpm >= 40 && bpm <= 240)) return { ok: false, error: 'Use a tempo between 40 and 240 BPM.' };
      if (analysis) analysis = { ...analysis, bpm };
      runAnalysis('captured', 'Lead vocal');
      return ok;
    },
    openAudioFile() {
      // The browser preview opens a file input instead; this path exists for contract tests.
      runAnalysis('file', 'Example vocal.wav');
      return { ok: true, name: 'Example vocal.wav' };
    },
    analyzePcm(req) {
      if (!req || typeof req.samplesBase64 !== 'string' || !(req.sampleRate > 0)) return { ok: false, error: 'Missing audio.' };
      const seconds = base64SampleCount(req.samplesBase64) / req.sampleRate;
      if (seconds > PCM_MAX_SECONDS + 0.5) {
        emit('error', { code: 'too_long', message: 'That file is longer than 3 minutes. Trim it or capture a section.' });
        return { ok: false, error: 'too_long' };
      }
      if (seconds < 1) {
        emit('error', { code: 'decode_failed', message: 'That file is too short to read. Use at least a few bars.' });
        return { ok: false, error: 'too_short' };
      }
      runAnalysis('file', req.name || 'Audio file');
      return ok;
    },
    cancel() {
      clearJobs();
      if (capture.state !== 'idle') setCapture('idle', 0);
      return ok;
    },
    setChord(segmentId, symbol) {
      const p = parseChord(symbol);
      if (!isParsed(p)) return { ok: false, error: p.error };
      const sym = formatChord(p, 'pop');
      analysis ??= mockAnalysis();
      analysis = { ...analysis, segments: analysis.segments.map((s) => (s.id === segmentId ? { ...s, symbol: sym, confidence: 1, userCorrected: true, userLocked: true } : s)) };
      locked.add(segmentId);
      return { ok: true, symbol: sym };
    },
    lock(ids, on) {
      for (const id of ids) on ? locked.add(id) : locked.delete(id);
      return ok;
    },
    generate({ seed }) {
      analysis ??= mockAnalysis();
      const a = analysis;
      later(380, () => emit('suggestionsReady', { schemaVersion: SCHEMA_VERSION, seed, suggestions: generateSet(a, seed, locked, controls()) }));
      return ok;
    },
    audition({ target, id, play }) {
      stopAudition();
      if (!play) { emit('auditionState', { playing: false, beat: 0, target, id }); return ok; }
      const bpm = song?.bpm ?? analysis?.bpm ?? 92;
      let start = 0, length = (analysis?.bars ?? 32) * 4;
      if (song && (target === 'section' || target === 'song')) {
        const sec = target === 'section' ? song.sections[parseInt(id ?? '', 10)] : undefined;
        start = sec ? sec.startBar * song.beatsPerBar : 0;
        length = sec ? sec.bars * song.beatsPerBar : songBeats(song);
      }
      const STEP = 50;
      audition.beat = 0;
      const tick = () => {
        audition.beat = (audition.beat + (STEP / 1000) * (bpm / 60)) % length;
        emit('auditionState', { playing: true, beat: start + audition.beat, target, id });
      };
      emit('auditionState', { playing: true, beat: start, target, id });
      audition.timer = setInterval(tick, Math.max(1, STEP * scale));
      return ok;
    },
    buildSong(req) {
      if (job === 'analysis' || job === 'capture') return { ok: false, error: 'Wait for the current job to finish.' };
      clearJobs();
      job = 'song';
      const online = req.useOnlineAi && ai.hasKey;
      if (req.useOnlineAi && !ai.hasKey)
        emit('error', { code: 'ai_unavailable', message: 'No API key set, so the description was read offline.' });
      runStages([['song', online ? 2200 : 1500]], () => {
        job = null;
        if (/\bfail\b/i.test(req.description)) {
          emit('error', { code: 'song_failed', message: 'The song builder stopped early. Try again, or remove a few moods.' });
          return;
        }
        song = { ...mockSong(req, analysis ?? mockAnalysis()), interpreter: online ? 'online' : 'offline' };
        emit('songReady', song);
      });
      return ok;
    },
    chooseSectionOption(sectionIndex, optionIndex) {
      const sec = song?.sections[sectionIndex];
      if (!song || !sec || !sec.options[optionIndex]) return { ok: false, error: 'No such option.' };
      song = { ...song, sections: song.sections.map((s, i) => (i === sectionIndex ? { ...s, chosen: optionIndex } : s)) };
      return ok;
    },
    setProgression(p) {
      const unparsed = p.chords.filter((c) => !isParsed(parseChord(c.symbol))).map((c) => c.symbol);
      return { ok: unparsed.length === 0, count: p.chords.length - unparsed.length, unparsed };
    },
    exportMidi() { return ok; },
    getSession() { return {}; },
    setSessionValue() { return true; },
    setAiKey(key) {
      const k = key.trim();
      if (!k) { ai = { hasKey: false, maskedKey: '' }; return ai; }
      if (!/^sk-[A-Za-z0-9_-]{16,}$/.test(k)) { ai = { ...ai, lastError: 'That does not look like an API key. It starts with sk-.' }; return ai; }
      ai = { hasKey: true, maskedKey: `${k.slice(0, 7)}...${k.slice(-4)}` };
      return ai;
    },
    getAiStatus() { return ai; },
    parseChord(symbol) {
      const p = parseChord(symbol);
      return isParsed(p) ? { ok: true, symbol: formatChord(p, 'pop') } : { ok: false, error: p.error };
    },
  };

  return {
    native: false,
    async call(name, ...args) {
      const fn = impl[name] as (...a: unknown[]) => unknown;
      return JSON.parse(JSON.stringify(fn(...args) ?? null));
    },
    on(name, fn) {
      if (!listeners.has(name)) listeners.set(name, new Set());
      const set = listeners.get(name)!;
      const f = fn as (p: unknown) => void;
      set.add(f);
      return () => set.delete(f);
    },
    prime(p) {
      if (p.analysis) analysis = p.analysis;
      if (p.song) song = p.song;
    },
  };
}
