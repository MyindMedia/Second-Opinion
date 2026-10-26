// Lightweight mirror of src/core/theory for display-only work in the UI (rendering symbols,
// mini keyboards, voice-leading diagrams). The C++ core stays the source of truth: inside the
// plugin, typed chords are validated by the native parseChord() function.

export type Notation = 'pop' | 'jazz';

interface QualityDef {
  intervals: number[];
  pop: string;
  jazz: string;
  aliases: string[];
}

const Q: Record<string, QualityDef> = {
  maj: { intervals: [0, 4, 7], pop: '', jazz: '', aliases: ['', 'maj', 'M', 'major'] },
  min: { intervals: [0, 3, 7], pop: 'm', jazz: '-', aliases: ['m', 'min', 'mi', '-', 'minor'] },
  dim: { intervals: [0, 3, 6], pop: 'dim', jazz: '°', aliases: ['dim', '°', 'o'] },
  aug: { intervals: [0, 4, 8], pop: 'aug', jazz: '+', aliases: ['aug', '+', '#5', '♯5'] },
  sus2: { intervals: [0, 2, 7], pop: 'sus2', jazz: 'sus2', aliases: ['sus2'] },
  sus4: { intervals: [0, 5, 7], pop: 'sus4', jazz: 'sus4', aliases: ['sus4', 'sus'] },
  six: { intervals: [0, 4, 7, 9], pop: '6', jazz: '6', aliases: ['6', 'maj6', 'M6'] },
  m6: { intervals: [0, 3, 7, 9], pop: 'm6', jazz: '-6', aliases: ['m6', 'min6', '-6'] },
  dom7: { intervals: [0, 4, 7, 10], pop: '7', jazz: '7', aliases: ['7', 'dom7'] },
  maj7: { intervals: [0, 4, 7, 11], pop: 'maj7', jazz: 'Δ7', aliases: ['maj7', 'M7', 'Δ7', 'Δ', 'ma7', '^7', '^'] },
  m7: { intervals: [0, 3, 7, 10], pop: 'm7', jazz: '-7', aliases: ['m7', 'min7', 'mi7', '-7'] },
  m7b5: { intervals: [0, 3, 6, 10], pop: 'm7♭5', jazz: 'ø7', aliases: ['m7b5', 'm7♭5', 'ø7', 'ø', '-7b5', '-7♭5', 'min7b5'] },
  dim7: { intervals: [0, 3, 6, 9], pop: 'dim7', jazz: '°7', aliases: ['dim7', '°7', 'o7'] },
  add9: { intervals: [0, 4, 7, 14], pop: 'add9', jazz: 'add9', aliases: ['add9', 'add2', '(add9)'] },
  dom9: { intervals: [0, 4, 7, 10, 14], pop: '9', jazz: '9', aliases: ['9'] },
  maj9: { intervals: [0, 4, 7, 11, 14], pop: 'maj9', jazz: 'Δ9', aliases: ['maj9', 'M9', 'Δ9', 'ma9'] },
  m9: { intervals: [0, 3, 7, 10, 14], pop: 'm9', jazz: '-9', aliases: ['m9', 'min9', '-9'] },
  sus7: { intervals: [0, 5, 7, 10], pop: '7sus4', jazz: '7sus4', aliases: ['7sus4', '7sus', '9sus4'] },
};

const LETTER_PC: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
const FLAT_NAMES = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'G♭', 'G', 'A♭', 'A', 'B♭', 'B'];
const AUTO_NAMES = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'];

export interface ParsedChord {
  root: string;      // spelled, e.g. "D♭"
  rootPc: number;
  quality: string;   // key of Q
  bass?: string;
  bassPc: number;
  pcs: number[];     // chord-tone pitch classes (root first)
  intervals: number[];
}

export const wrap = (v: number) => ((v % 12) + 12) % 12;

function parsePitch(text: string): { name: string; pc: number; len: number } | null {
  const m = /^([A-Ga-g])((?:♭|♯|𝄫|𝄪|b|#){0,2})/u.exec(text);
  if (!m) return null;
  const letter = m[1].toUpperCase();
  let acc = 0;
  for (const ch of m[2]) {
    if (ch === '♭' || ch === 'b') acc -= 1;
    else if (ch === '♯' || ch === '#') acc += 1;
    else if (ch === '𝄫') acc -= 2;
    else if (ch === '𝄪') acc += 2;
  }
  const accGlyph = acc === -1 ? '♭' : acc === 1 ? '♯' : acc === -2 ? '𝄫' : acc === 2 ? '𝄪' : '';
  return { name: letter + accGlyph, pc: wrap(LETTER_PC[letter] + acc), len: m[0].length };
}

const ALIASES: [string, string][] = Object.entries(Q)
  .flatMap(([k, d]) => d.aliases.map((a) => [a, k] as [string, string]))
  .sort((a, b) => b[0].length - a[0].length);

export function parseChord(input: string): ParsedChord | { error: string } {
  const text = input.trim();
  if (!text) return { error: 'Type a chord, for example G/B or Am7' };
  const root = parsePitch(text);
  if (!root) return { error: 'Start with a note name (A to G)' };

  let rest = text.slice(root.len);
  let bassText = '';
  const slash = rest.lastIndexOf('/');
  if (slash >= 0) {
    bassText = rest.slice(slash + 1).trim();
    rest = rest.slice(0, slash).trim();
    if (!bassText) return { error: 'Add a bass note after the slash, for example G/B' };
  }

  const match = ALIASES.find(([a]) => a === rest);
  if (!match) return { error: `Unknown chord type "${rest}". Try m, 7, maj7, m7, m7b5, dim7, sus4, add9 or 9` };

  let bass: string | undefined;
  let bassPc = root.pc;
  if (bassText) {
    const b = parsePitch(bassText);
    if (!b || b.len !== bassText.length) return { error: 'The bass after the slash must be a note name, for example /B or /F#' };
    if (b.pc !== root.pc) {
      bass = b.name;
      bassPc = b.pc;
    }
  }

  const q = Q[match[1]];
  return {
    root: root.name,
    rootPc: root.pc,
    quality: match[1],
    bass,
    bassPc,
    intervals: q.intervals,
    pcs: q.intervals.map((i) => wrap(root.pc + i)),
  };
}

export const isParsed = (r: ParsedChord | { error: string }): r is ParsedChord => !('error' in r);

/** Canonical symbol in the chosen notation (Pop: Cmaj7, Jazz: CΔ7). */
export function formatChord(c: ParsedChord, notation: Notation): string {
  const q = Q[c.quality];
  return c.root + (notation === 'pop' ? q.pop : q.jazz) + (c.bass ? '/' + c.bass : '');
}

export function convertSymbol(symbol: string, notation: Notation): string {
  const p = parseChord(symbol);
  return isParsed(p) ? formatChord(p, notation) : symbol;
}

export const pcName = (pc: number) => AUTO_NAMES[wrap(pc)];
export const pcNameFlat = (pc: number) => FLAT_NAMES[wrap(pc)];

function escapeHtml(s: string) {
  return s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]!);
}

/** Chord symbol as typeset HTML: root and accidental full size, quality, extensions raised
    (F m⁹, D♭ maj⁷/F). */
export function chordHtml(symbol: string, notation: Notation = 'pop'): string {
  const display = convertSymbol(symbol, notation);
  const m = /^([A-G])((?:♭|♯|𝄫|𝄪)?)([^/]*)(?:\/(.+))?$/u.exec(display);
  if (!m) return `<span class="ch">${escapeHtml(display)}</span>`;
  const [, root, acc, qual, bass] = m;
  const qm = /^(maj|m|dim|aug|sus|add|-|Δ|ø|°|\+)?(.*)$/u.exec(qual)!;
  const base = qm[1] ?? '';
  const ext = qm[2] ?? '';
  return (
    `<span class="ch">` +
    `<span class="ch-root">${root}</span>` +
    (acc ? `<span class="ch-acc">${acc}</span>` : '') +
    (base ? `<span class="ch-q">${escapeHtml(base)}</span>` : '') +
    (ext ? `<sup class="ch-ext">${escapeHtml(ext)}</sup>` : '') +
    (bass ? `<span class="ch-slash">/</span><span class="ch-bass">${escapeHtml(bass)}</span>` : '') +
    `</span>`
  );
}

/** Interval name of a melody pitch class against a chord root, as a musician would say it. */
export function melodyRelation(melodyPc: number, chord: ParsedChord): string {
  const rel = wrap(melodyPc - chord.rootPc);
  const inChord = chord.intervals.map((i) => i % 12);
  const names: Record<number, string> = {
    0: 'root', 1: '♭9', 2: '9th', 3: inChord.includes(3) ? '♭3rd' : '♯9', 4: '3rd', 5: inChord.includes(5) ? '4th' : '11th',
    6: inChord.includes(6) ? '♭5th' : '♯11', 7: '5th', 8: inChord.includes(8) ? '♯5th' : '♭13', 9: inChord.includes(9) && chord.quality === 'dim7' ? '𝄫7th' : inChord.includes(9) ? '6th' : '13th',
    10: '♭7th', 11: '7th',
  };
  return names[rel];
}

/** Close-position voicing nearest to `prev` (sorted MIDI notes), mirroring SimpleVoicer.cpp. */
export function voice(chord: ParsedChord, prev?: number[]): number[] {
  let pcs = chord.pcs.slice();
  if (pcs.length > 4) pcs.splice(2, 1);
  let best: number[] = [];
  let bestCost = Infinity;
  for (let rot = 0; rot < pcs.length; rot++) {
    for (let base = 48; base <= 72; base += 12) {
      const v: number[] = [];
      let last = -1;
      for (let i = 0; i < pcs.length; i++) {
        let n = base + pcs[(i + rot) % pcs.length];
        while (n <= last) n += 12;
        v.push(n);
        last = n;
      }
      if (v[0] < 55 || v[v.length - 1] > 84) continue;
      const cost = prev && prev.length
        ? v.reduce((s, n, i) => s + Math.abs(n - prev[Math.min(i, prev.length - 1)]), 0)
        : Math.abs((v[0] + v[v.length - 1]) / 2 - 66);
      if (cost < bestCost) { bestCost = cost; best = v; }
    }
  }
  return best;
}
