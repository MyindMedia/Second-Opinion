import { describe, expect, it } from 'vitest';
import { isSong, STRUCTURES } from '../bridge/contract';
import { mockAnalysis } from './mock';
import { arrangementChords, defaultBuildRequest, mockSong, readDescription, resolveStructure, sectionChords, songBeats, transitionText } from './song';

const analysis = mockAnalysis();

describe('song mock', () => {
  it('is deterministic for the same request', () => {
    const a = mockSong(defaultBuildRequest(3), analysis);
    const b = mockSong(defaultBuildRequest(3), analysis);
    expect(JSON.stringify(a)).toBe(JSON.stringify(b));
    expect(isSong(a)).toBe(true);
  });

  it('changes non-idea options with the seed but keeps the idea as heard', () => {
    const songs = [0, 1, 2, 3, 4].map((seed) => mockSong(defaultBuildRequest(seed), analysis));
    const sig = (s: ReturnType<typeof mockSong>) => s.sections.filter((x) => !x.isIdea).map((x) => x.options.map((o) => o.title).join('|')).join('/');
    expect(new Set(songs.map(sig)).size).toBeGreaterThan(1);
    for (const s of songs) {
      const idea = s.sections.find((x) => x.isIdea)!;
      expect(idea.options[0].chords[0].symbol).toBe(analysis.segments[0].symbol);
    }
  });

  it('builds contiguous sections whose options fill every bar', () => {
    for (const structure of STRUCTURES) {
      for (const ideaSection of ['verse', 'chorus', 'hook', 'loop'] as const) {
        const song = mockSong({ ...defaultBuildRequest(7), structure, ideaSection }, analysis);
        let bar = 0;
        for (const sec of song.sections) {
          expect(sec.startBar).toBe(bar);
          bar += sec.bars;
          expect(sec.options.length).toBeGreaterThanOrEqual(1);
          for (const o of sec.options) {
            expect(o.chords.reduce((n, c) => n + c.beats, 0)).toBe(sec.bars * song.beatsPerBar);
            expect(o.energy).toBeGreaterThanOrEqual(0);
            expect(o.tension).toBeLessThanOrEqual(100);
          }
        }
        expect(song.sections.some((s) => s.isIdea)).toBe(true);
      }
    }
  });

  it('concatenates the chosen arrangement to exactly the song length', () => {
    const song = mockSong(defaultBuildRequest(), analysis);
    const all = arrangementChords(song, 'song', 0);
    expect(all.reduce((n, c) => n + c.beats, 0)).toBe(songBeats(song));
    for (let i = 1; i < all.length; i++) expect(all[i].startBeat).toBe(all[i - 1].startBeat + all[i - 1].beats);
    const sec = arrangementChords(song, 'section', 3);
    expect(sec[0].startBeat).toBe(song.sections[3].startBar * 4);
    expect(sectionChords(song, 99)).toEqual([]);
  });

  it('survives a JSON round trip unchanged', () => {
    const song = mockSong(defaultBuildRequest(), analysis);
    expect(JSON.parse(JSON.stringify(song))).toEqual(song);
  });

  it('reads moods and structure from the description offline', () => {
    expect(readDescription('dark late-night R&B, hopeful chorus', []).moods).toEqual(['dark', 'hopeful']);
    expect(resolveStructure({ structure: 'auto', description: 'gritty trap beat', moods: [] })).toBe('hiphop-trap');
    expect(resolveStructure({ structure: 'auto', description: '', moods: ['euphoric'] })).toBe('edm');
    expect(resolveStructure({ structure: 'gospel', description: 'trap', moods: [] })).toBe('gospel');
  });

  it('explains the hand-off into the next section', () => {
    const song = mockSong(defaultBuildRequest(), analysis);
    const t = transitionText(song, 1);
    expect(t.nextLabel).toBe(song.sections[2].label);
    expect(t.text.length).toBeGreaterThan(10);
    expect(transitionText(song, song.sections.length - 1).text).toMatch(/last section/);
  });

  it('uses no em dashes in any copy', () => {
    expect(JSON.stringify(mockSong(defaultBuildRequest(), analysis))).not.toMatch(/\u2014/);
  });
});
