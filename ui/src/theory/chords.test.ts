import { describe, expect, it } from 'vitest';
import { chordHtml, convertSymbol, formatChord, isParsed, melodyRelation, parseChord, voice } from './chords';

const must = (s: string) => {
  const p = parseChord(s);
  if (!isParsed(p)) throw new Error(`${s}: ${p.error}`);
  return p;
};

describe('UI chord helpers mirror the C++ core', () => {
  it('parses the PRD examples', () => {
    expect(formatChord(must('G/B'), 'pop')).toBe('G/B');
    expect(formatChord(must('F#m7b5'), 'pop')).toBe('F♯m7♭5');
    expect(formatChord(must('Dbmaj7/F'), 'pop')).toBe('D♭maj7/F');
    expect(formatChord(must('Csus2'), 'pop')).toBe('Csus2');
  });

  it('converts between Pop and Jazz', () => {
    expect(convertSymbol('Cmaj7', 'jazz')).toBe('CΔ7');
    expect(convertSymbol('Bm7b5', 'jazz')).toBe('Bø7');
    expect(convertSymbol('C-7', 'pop')).toBe('Cm7');
  });

  it('gives human errors', () => {
    expect(parseChord('H7')).toEqual({ error: 'Start with a note name (A to G)' });
    expect('error' in parseChord('G/')).toBe(true);
    expect('error' in parseChord('Cxyz')).toBe(true);
  });

  it('typesets extensions as superscripts', () => {
    expect(chordHtml('Fm9')).toContain('<sup class="ch-ext">9</sup>');
    expect(chordHtml('D♭maj7/F')).toContain('<span class="ch-bass">F</span>');
  });

  it('names the melody relation', () => {
    expect(melodyRelation(0, must('B♭9'))).toBe('9th');
    expect(melodyRelation(0, must('Fm9'))).toBe('5th');
  });

  it('voices within range, deterministically', () => {
    const a = voice(must('Fm9'));
    expect(a).toEqual(voice(must('Fm9')));
    for (const n of a) expect(n).toBeGreaterThanOrEqual(55);
  });
});
