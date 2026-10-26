import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { encodePcmBase64 } from '../audio/pcm';
import { defaultBuildRequest } from '../state/song';
import type { BackendEventName, BackendEvents } from './contract';
import { EVENTS, isAnalysisResult, isSong, isSuggestionsReady, NATIVE_FUNCTIONS, SCHEMA_VERSION } from './contract';
import { createMockBackend } from './mock-backend';

function record(b: ReturnType<typeof createMockBackend>) {
  const log: { name: BackendEventName; payload: unknown }[] = [];
  for (const name of EVENTS) b.on(name, (payload) => log.push({ name, payload }));
  const last = <E extends BackendEventName>(n: E) => [...log].reverse().find((e) => e.name === n)?.payload as BackendEvents[E] | undefined;
  return { log, last, names: () => log.map((e) => e.name) };
}

describe('bridge contract mock round-trips', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('lists every function and event exactly once', () => {
    expect(new Set(NATIVE_FUNCTIONS).size).toBe(NATIVE_FUNCTIONS.length);
    expect(NATIVE_FUNCTIONS).toContain('analyzePcm');
    expect(EVENTS).toEqual(['progress', 'captureState', 'analysisReady', 'suggestionsReady', 'songReady', 'auditionState', 'transport', 'error']);
  });

  it('capture: armed, recording, done, progress, analysisReady, suggestionsReady', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    expect(await b.call('startCapture')).toEqual({ ok: true });
    await vi.runAllTimersAsync();
    const states = r.log.filter((e) => e.name === 'captureState').map((e) => (e.payload as BackendEvents['captureState']).state);
    expect(states[0]).toBe('armed');
    expect(states).toContain('recording');
    expect(states[states.length - 1]).toBe('done');
    const n = r.names();
    expect(n.indexOf('analysisReady')).toBeGreaterThan(n.indexOf('progress'));
    expect(n.indexOf('suggestionsReady')).toBeGreaterThan(n.indexOf('analysisReady'));
    expect(isAnalysisResult(r.last('analysisReady'))).toBe(true);
    expect(isSuggestionsReady(r.last('suggestionsReady'))).toBe(true);
    const stages = new Set(r.log.filter((e) => e.name === 'progress').map((e) => (e.payload as BackendEvents['progress']).stage));
    expect([...stages]).toEqual(['preparing', 'transcribing', 'harmony', 'suggestions']);
  });

  it('analyzePcm: accepts 22050 Hz base64 PCM and names the source', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    const pcm = new Float32Array(22050 * 4).map((_, i) => Math.sin(i / 20));
    const ack = await b.call('analyzePcm', { schemaVersion: SCHEMA_VERSION, sampleRate: 22050, samplesBase64: encodePcmBase64(pcm), name: 'Hook idea.wav' });
    expect(ack?.ok).toBe(true);
    await vi.runAllTimersAsync();
    expect(r.last('analysisReady')?.sourceName).toBe('Hook idea.wav');
    expect(r.last('analysisReady')?.source).toBe('file');
  });

  it('analyzePcm: rejects audio over 3 minutes with a too_long error', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    const ack = await b.call('analyzePcm', { schemaVersion: SCHEMA_VERSION, sampleRate: 22050, samplesBase64: encodePcmBase64(new Float32Array(22050 * 181)), name: 'Long.wav' });
    expect(ack?.ok).toBe(false);
    expect(r.last('error')?.code).toBe('too_long');
  });

  it('cancel stops analysis before analysisReady', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    await b.call('openAudioFile');
    await vi.advanceTimersByTimeAsync(5);
    await b.call('cancel');
    await vi.runAllTimersAsync();
    expect(r.names()).not.toContain('analysisReady');
  });

  it('setChord normalizes the symbol and lock/generate keep it', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    expect(await b.call('setChord', 'o0', 'Xq9')).toEqual({ ok: false, error: expect.any(String) });
    expect(await b.call('setChord', 'o0', 'Fm7')).toEqual({ ok: true, symbol: 'Fm7' });
    await b.call('generate', { seed: 2 });
    await vi.runAllTimersAsync();
    const ev = r.last('suggestionsReady')!;
    expect(ev.seed).toBe(2);
    for (const sg of ev.suggestions) expect(sg.chords.find((c) => c.startBeat === 0)?.symbol).toBe('Fm7');
  });

  it('buildSong: progress on the song stage, then a valid, deterministic songReady', async () => {
    const run = async () => {
      const b = createMockBackend({ timeScale: 0.01 });
      const r = record(b);
      await b.call('buildSong', defaultBuildRequest(5));
      await vi.runAllTimersAsync();
      expect(r.log.some((e) => e.name === 'progress' && (e.payload as BackendEvents['progress']).stage === 'song')).toBe(true);
      return r.last('songReady');
    };
    const a = await run();
    const b = await run();
    expect(isSong(a)).toBe(true);
    expect(a).toEqual(b);
    expect(a?.interpreter).toBe('offline');
  });

  it('buildSong with online AI but no key falls back offline and says so', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    await b.call('buildSong', { ...defaultBuildRequest(), useOnlineAi: true });
    await vi.runAllTimersAsync();
    expect(r.last('error')?.code).toBe('ai_unavailable');
    expect(r.last('songReady')?.interpreter).toBe('offline');

    const status = await b.call('setAiKey', 'sk-ant-api03-abcdefghijklmnop1234');
    expect(status).toEqual({ hasKey: true, maskedKey: 'sk-ant-...1234' });
    expect(JSON.stringify(status)).not.toContain('abcdefghijklmnop');
    await b.call('buildSong', { ...defaultBuildRequest(), useOnlineAi: true });
    await vi.runAllTimersAsync();
    expect(r.last('songReady')?.interpreter).toBe('online');
    expect((await b.call('setAiKey', ''))?.hasKey).toBe(false);
  });

  it('chooseSectionOption, setProgression and audition answer with contract shapes', async () => {
    const b = createMockBackend({ timeScale: 0.01 });
    const r = record(b);
    await b.call('buildSong', defaultBuildRequest());
    await vi.runAllTimersAsync();
    expect(await b.call('chooseSectionOption', 1, 2)).toEqual({ ok: true });
    expect((await b.call('chooseSectionOption', 1, 9))?.ok).toBe(false);
    expect(await b.call('setProgression', { id: 'x', title: 'x', bpm: 92, chords: [{ symbol: 'Fm9', beats: 4 }, { symbol: 'D♭maj7', beats: 4 }] }))
      .toEqual({ ok: true, count: 2, unparsed: [] });
    await b.call('audition', { target: 'section', id: '1', play: true });
    await vi.advanceTimersByTimeAsync(10);
    const st = r.last('auditionState')!;
    const song = r.last('songReady')!;
    expect(st.playing).toBe(true);
    expect(st.beat).toBeGreaterThanOrEqual(song.sections[1].startBar * 4);
    await b.call('audition', { target: 'section', id: '1', play: false });
    expect(r.last('auditionState')?.playing).toBe(false);
  });
});
