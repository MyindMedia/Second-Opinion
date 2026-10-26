// Build out: labels, the browser-preview song generator (stands in for the C++ song builder and
// offline mood engine) and pure helpers the UI uses with real or mock songs.

import type {
  AnalysisResult, BuildSongRequest, HarmonicFunction, IdeaSection, Mood, SectionKind, SectionOption, Song,
  SongChord, SongSection, Structure,
} from '../bridge/contract';
import { SCHEMA_VERSION } from '../bridge/contract';
import { mockSuggestions } from './mock';

// ---------------------------------------------------------------------------------------------
// Labels (the UI owns them; the contract carries ids)
// ---------------------------------------------------------------------------------------------

export const MOOD_LABEL: Record<Mood, string> = {
  dark: 'Dark', hopeful: 'Hopeful', melancholic: 'Melancholic', euphoric: 'Euphoric', dreamy: 'Dreamy', tense: 'Tense',
  romantic: 'Romantic', aggressive: 'Aggressive', chill: 'Chill', nostalgic: 'Nostalgic', cinematic: 'Cinematic',
  soulful: 'Soulful', mysterious: 'Mysterious', triumphant: 'Triumphant', bittersweet: 'Bittersweet', gritty: 'Gritty',
};

export const IDEA_LABEL: Record<IdeaSection, string> = { verse: 'Verse', chorus: 'Chorus', hook: 'Hook', loop: 'Loop' };

export const STRUCTURE_LABEL: Record<Structure, string> = {
  auto: 'Auto', pop: 'Pop', rnb: 'R&B', 'hiphop-trap': 'Hip-hop / Trap', gospel: 'Gospel', edm: 'EDM', cinematic: 'Cinematic',
};

export const KIND_LABEL: Record<SectionKind, string> = {
  intro: 'Intro', verse: 'Verse', pre: 'Pre-chorus', chorus: 'Chorus', hook: 'Hook', post: 'Post-chorus', bridge: 'Bridge',
  build: 'Build', drop: 'Drop', breakdown: 'Breakdown', vamp: 'Vamp', outro: 'Outro', loop: 'Loop',
};

// ---------------------------------------------------------------------------------------------
// Pure helpers (work on any Song, real or mock)
// ---------------------------------------------------------------------------------------------

export const songBars = (song: Song) => song.sections.reduce((n, s) => Math.max(n, s.startBar + s.bars), 0);
export const songBeats = (song: Song) => songBars(song) * song.beatsPerBar;

export const chosenOption = (sec: SongSection): SectionOption | undefined => sec.options[sec.chosen] ?? sec.options[0];

/** Chords of one section option with absolute song beats, filled to exactly the section length. */
export function sectionChords(song: Song, index: number, optionIndex?: number): SongChord[] {
  const sec = song.sections[index];
  if (!sec) return [];
  const opt = sec.options[optionIndex ?? sec.chosen] ?? sec.options[0];
  if (!opt) return [];
  const start = sec.startBar * song.beatsPerBar;
  const len = sec.bars * song.beatsPerBar;
  const out: SongChord[] = [];
  for (const c of [...opt.chords].sort((a, b) => a.startBeat - b.startBeat)) {
    if (c.startBeat >= len) break;
    out.push({ ...c, startBeat: start + c.startBeat, beats: Math.min(c.beats, len - c.startBeat) });
  }
  // Close gaps so the drag strip gets back-to-back chords: stretch each chord to the next one.
  for (let i = 0; i < out.length; i++) {
    const next = i + 1 < out.length ? out[i + 1].startBeat : start + len;
    out[i].beats = Math.max(out[i].beats, next - out[i].startBeat);
  }
  return out;
}

/** The chosen arrangement, whole song or one section, as setProgression chords. */
export function arrangementChords(song: Song, scope: 'section' | 'song', sectionIndex: number): SongChord[] {
  if (scope === 'section') return sectionChords(song, sectionIndex);
  return song.sections.flatMap((_, i) => sectionChords(song, i));
}

export function sectionRange(song: Song, i: number) {
  const s = song.sections[i];
  return s ? `bars ${s.startBar + 1} to ${s.startBar + s.bars}` : '';
}

/** Plain sentence on how an option hands off into the next section (used when C++ sends none). */
export function transitionText(song: Song, index: number, optionIndex?: number): { text: string; from?: SongChord; to?: SongChord; nextLabel?: string } {
  const sec = song.sections[index];
  const opt = sec?.options[optionIndex ?? sec.chosen];
  const mine = sectionChords(song, index, optionIndex);
  const last = mine[mine.length - 1];
  if (!sec || !opt || !last) return { text: '' };
  const next = song.sections[index + 1];
  if (!next) {
    const end = last.fn === 'tonic' ? 'so the song ends at home.' : 'so the song ends open, without fully resolving.';
    return { text: opt.transition ?? `This is the last section. It ends on ${last.symbol} (${last.roman}), ${end}`, from: last };
  }
  const first = sectionChords(song, index + 1)[0];
  if (!first) return { text: opt.transition ?? '', from: last, nextLabel: next.label };
  let text: string;
  if (last.symbol === first.symbol) text = `Holds ${last.symbol} across the line, so ${next.label} starts without a seam.`;
  else if (last.fn === 'dominant') text = `Ends on ${last.symbol} (${last.roman}), which pulls straight into ${first.symbol} at the top of ${next.label}.`;
  else if (last.fn === 'predominant') text = `Ends on ${last.symbol} (${last.roman}), a lift that leaves the door open for ${next.label} to start on ${first.symbol}.`;
  else if (last.fn === 'tonic') text = `Ends at home on ${last.symbol}, so ${next.label} starts fresh on ${first.symbol}.`;
  else text = `Ends on ${last.symbol}, an outside color that makes ${first.symbol} in ${next.label} sound like a return.`;
  return { text: opt.transition ?? text, from: last, to: first, nextLabel: next.label };
}

// ---------------------------------------------------------------------------------------------
// Offline mood engine (browser preview stand-in for src/core/song)
// ---------------------------------------------------------------------------------------------

const MOOD_WORDS: [RegExp, Mood][] = [
  [/\bdark|night|moody|shadow/, 'dark'], [/hope|uplift|bright/, 'hopeful'], [/sad|melanchol|blue\b|lonely/, 'melancholic'],
  [/euphori|happy|party|joy/, 'euphoric'], [/dream|float|haz/, 'dreamy'], [/tense|anxious|suspense/, 'tense'],
  [/love|romant|sensual|slow jam/, 'romantic'], [/aggress|hard\b|angry|rage/, 'aggressive'], [/chill|lo-?fi|laid.?back|smooth/, 'chill'],
  [/nostalg|throwback|old.?school|90s/, 'nostalgic'], [/cinema|film|epic|score/, 'cinematic'], [/soul|gospel|church/, 'soulful'],
  [/myster|eerie|haunt/, 'mysterious'], [/triumph|anthem|victor/, 'triumphant'], [/bittersweet/, 'bittersweet'], [/gritty|grime|dirty|raw/, 'gritty'],
];

const STRUCTURE_WORDS: [RegExp, Exclude<Structure, 'auto'>][] = [
  [/r&b|rnb|r and b|neo.?soul/, 'rnb'], [/trap|hip.?hop|rap|drill|boom.?bap/, 'hiphop-trap'], [/gospel|church|choir/, 'gospel'],
  [/edm|house|dance|techno|drop\b/, 'edm'], [/cinema|film|score|trailer/, 'cinematic'], [/\bpop\b/, 'pop'],
];

export function readDescription(description: string, chips: Mood[]): { moods: Mood[]; structureHint?: Exclude<Structure, 'auto'> } {
  const d = description.toLowerCase();
  const moods = [...chips];
  for (const [re, m] of MOOD_WORDS) if (re.test(d) && !moods.includes(m)) moods.push(m);
  const structureHint = STRUCTURE_WORDS.find(([re]) => re.test(d))?.[1];
  return { moods, structureHint };
}

export function resolveStructure(req: Pick<BuildSongRequest, 'structure' | 'description' | 'moods'>): Exclude<Structure, 'auto'> {
  if (req.structure !== 'auto') return req.structure;
  const { moods, structureHint } = readDescription(req.description, req.moods);
  if (structureHint) return structureHint;
  const has = (...m: Mood[]) => m.some((x) => moods.includes(x));
  if (has('aggressive', 'gritty')) return 'hiphop-trap';
  if (has('euphoric')) return 'edm';
  if (has('cinematic', 'triumphant')) return 'cinematic';
  if (has('soulful')) return 'gospel';
  if (has('romantic', 'chill')) return 'rnb';
  return 'pop';
}

const FORMS: Record<Exclude<Structure, 'auto'>, [SectionKind, number][]> = {
  pop: [['intro', 4], ['verse', 8], ['pre', 4], ['chorus', 8], ['verse', 8], ['pre', 4], ['chorus', 8], ['bridge', 8], ['chorus', 8], ['outro', 4]],
  rnb: [['intro', 4], ['verse', 8], ['chorus', 8], ['verse', 8], ['chorus', 8], ['bridge', 4], ['chorus', 8], ['outro', 8]],
  'hiphop-trap': [['intro', 4], ['hook', 8], ['verse', 16], ['hook', 8], ['verse', 16], ['hook', 8], ['outro', 4]],
  gospel: [['intro', 4], ['verse', 8], ['chorus', 8], ['verse', 8], ['chorus', 8], ['vamp', 8], ['chorus', 8], ['outro', 4]],
  edm: [['intro', 8], ['verse', 8], ['build', 8], ['drop', 8], ['breakdown', 8], ['build', 8], ['drop', 8], ['outro', 8]],
  cinematic: [['intro', 8], ['verse', 8], ['build', 8], ['chorus', 8], ['bridge', 8], ['chorus', 8], ['outro', 8]],
};

const ARC: Record<Exclude<Structure, 'auto'>, string> = {
  pop: 'Quiet verses, a climb in each pre-chorus, and the widest chords saved for the last chorus.',
  rnb: 'Low, close verses that open into a warm chorus, with a short bridge before the last lift.',
  'hiphop-trap': 'The hook carries the weight; long verses stay sparse so the flow has room.',
  gospel: 'Verses build into the chorus, then a vamp climbs bar by bar before the final chorus.',
  edm: 'Two build and drop cycles with a breakdown between them to reset the energy.',
  cinematic: 'Starts small and swells in waves, peaking in the last chorus before a long fade.',
};

/** Which kinds in a form the idea plays as, in order of preference. */
const IDEA_KINDS: Record<IdeaSection, SectionKind[]> = {
  verse: ['verse'],
  chorus: ['chorus', 'drop', 'hook'],
  hook: ['hook', 'chorus', 'drop'],
  loop: ['verse', 'breakdown'],
};

// Roman tokens per mode: [semitones above tonic, quality suffix, function]
type Tok = [number, string, HarmonicFunction];
const MINOR: Record<string, Tok> = {
  i: [0, 'm', 'tonic'], i7: [0, 'm7', 'tonic'], i9: [0, 'm9', 'tonic'], I: [0, '', 'color'], 'iiø7': [2, 'm7♭5', 'predominant'],
  III: [3, '', 'tonic'], IIImaj7: [3, 'maj7', 'tonic'], iv: [5, 'm', 'predominant'], iv7: [5, 'm7', 'predominant'],
  iv9: [5, 'm9', 'predominant'], IV9: [5, '9', 'predominant'], V7: [7, '7', 'dominant'], V7sus4: [7, '7sus4', 'dominant'],
  VI: [8, '', 'predominant'], VImaj7: [8, 'maj7', 'predominant'], VImaj9: [8, 'maj9', 'predominant'], VII: [10, '', 'dominant'],
  VIIsus4: [10, 'sus4', 'dominant'], '♭IImaj7': [1, 'maj7', 'predominant'],
};
const MAJOR: Record<string, Tok> = {
  I: [0, '', 'tonic'], Imaj7: [0, 'maj7', 'tonic'], Iadd9: [0, 'add9', 'tonic'], ii7: [2, 'm7', 'predominant'], ii9: [2, 'm9', 'predominant'],
  iii7: [4, 'm7', 'tonic'], IV: [5, '', 'predominant'], IVmaj7: [5, 'maj7', 'predominant'], iv6: [5, 'm6', 'color'],
  V: [7, '', 'dominant'], V7: [7, '7', 'dominant'], Vsus4: [7, 'sus4', 'dominant'], vi: [9, 'm', 'tonic'], vi7: [9, 'm7', 'tonic'],
  '♭VII': [10, '', 'color'], '♭VImaj7': [8, 'maj7', 'color'], 'V7/vi': [4, '7', 'dominant'],
};

const FLATS = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'G♭', 'G', 'A♭', 'A', 'B♭', 'B'];
const SHARPS = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
const LETTER: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

export function parseKey(key: string) {
  const m = /^([A-G])([♭♯b#]?)\s*(major|minor)?/i.exec(key.trim());
  const letter = (m?.[1] ?? 'C').toUpperCase();
  const acc = m?.[2] ?? '';
  const pc = (LETTER[letter] + (acc === '♭' || acc === 'b' ? -1 : acc === '♯' || acc === '#' ? 1 : 0) + 12) % 12;
  const minor = /minor/i.test(m?.[3] ?? '');
  const sharpKeys = minor ? [4, 11, 6, 1, 8] : [7, 2, 9, 4, 11, 6];
  const sharps = acc === '♯' || acc === '#' || (acc === '' && sharpKeys.includes(pc));
  return { pc, minor, names: sharps ? SHARPS : FLATS };
}

interface Template {
  title: string;
  explanation: string;
  energy: number;
  tension: number;
  tags: Mood[];
  minor: string;   // "i9:8 VImaj7:8" (beats default 4)
  major: string;
}

const T = (title: string, explanation: string, energy: number, tension: number, tags: Mood[], minor: string, major: string): Template =>
  ({ title, explanation, energy, tension, tags, minor, major });

const INTRO: Template[] = [
  T('Hold the tonic', 'Starts at home with one color chord, so the vocal enters on a clean bed.', 20, 15, ['chill', 'dreamy', 'dark', 'romantic'], 'i9:8 VImaj7:8', 'Imaj7:8 IVmaj7:8'),
  T('Tease the turnaround', 'Plays the chorus turnaround once, softly, so the hook feels familiar when it lands.', 30, 30, ['hopeful', 'nostalgic', 'euphoric', 'bittersweet'], 'VImaj7 VII IIImaj7 i7', 'IVmaj7 V vi7 Iadd9'),
  T('Pedal and sus', 'One bass note under a suspended chord builds quiet suspense before the first verse.', 25, 45, ['tense', 'mysterious', 'cinematic', 'gritty'], 'i:8 VIIsus4 VII', 'Iadd9:8 Vsus4 V'),
];
const VERSE: Template[] = [
  T('Low and steady', 'Two chords back and forth leave room for the lyric to lead.', 35, 20, ['chill', 'dark', 'romantic', 'dreamy'], 'i9:8 VImaj7:8', 'Imaj7:8 vi7:8'),
  T('Walk down', 'The bass steps down each bar, so every line feels like it sinks a little deeper.', 40, 35, ['melancholic', 'bittersweet', 'soulful', 'nostalgic'], 'i9 VII VImaj7 V7sus4', 'Imaj7 iii7 vi7 IVmaj7'),
  T('Dorian glow', 'A major IV inside the minor key keeps the verse moving without getting brighter.', 45, 40, ['soulful', 'hopeful', 'chill', 'romantic'], 'i7 IV9 i7 IV9', 'Imaj7 ii7 iii7 IVmaj7'),
  T('Dark pulse', 'Stays low with a half-step chord above home for a gritty, uneasy edge.', 50, 60, ['dark', 'aggressive', 'gritty', 'tense', 'mysterious'], 'i:8 ♭IImaj7 i', 'I:8 ♭VII IV'),
];
const PRE: Template[] = [
  T('Climb on iv', 'Rises through the chords that lead away from home, so the chorus feels earned.', 60, 55, ['hopeful', 'euphoric', 'triumphant', 'nostalgic'], 'iv7 VImaj7 VIIsus4 VII', 'IV V vi Vsus4'),
  T('Hold the dominant', 'Sits on V for two bars, so the chorus downbeat lands hard.', 65, 75, ['tense', 'aggressive', 'cinematic', 'dark'], 'iv9:8 V7sus4 V7', 'ii7:8 Vsus4 V7'),
  T('Borrowed sigh', 'A borrowed minor chord adds a sigh right before the lift.', 55, 50, ['bittersweet', 'nostalgic', 'romantic', 'melancholic'], 'VImaj7 iv7 iiø7 V7', 'IVmaj7 iv6 ii7 V7'),
];
const CHORUS: Template[] = [
  T('Lift to the relative major', 'Leans on the major chords of the key, so the chorus sounds brighter than the verse without changing key.', 80, 40, ['hopeful', 'euphoric', 'triumphant', 'romantic'], 'IIImaj7 VII VImaj7 VII', 'IV I V vi'),
  T('Big four-chord loop', 'The familiar circle: easy to sing over and strong on repeat.', 85, 35, ['euphoric', 'hopeful', 'nostalgic', 'bittersweet'], 'i VI III VII', 'I V vi IV'),
  T('Gospel push', 'Each chord leans into the next, the way a choir climbs into a big note.', 80, 65, ['soulful', 'triumphant', 'romantic'], 'iv9 V7 i9 IV9', 'IVmaj7 V7/vi vi7 ii9:2 V7:2'),
  T('Dark anthem', 'Keeps the minor center but moves the root every bar for a heavy, driving hook.', 90, 60, ['dark', 'aggressive', 'gritty', 'cinematic', 'tense'], 'i VI VII V7', 'vi IV I V'),
  T('Floating chorus', 'Major sevenths and ninths blur the edges for a wide, dreamy hook.', 70, 30, ['dreamy', 'chill', 'romantic', 'mysterious', 'melancholic'], 'VImaj9 IIImaj7 VImaj9 VIIsus4', 'IVmaj7 Imaj7 IVmaj7 Iadd9'),
];
const BRIDGE: Template[] = [
  T('Half-step turn', 'Moves to the chord a half step above home for a new color nobody has heard yet.', 55, 70, ['mysterious', 'cinematic', 'tense', 'dark'], '♭IImaj7 VImaj7 ♭IImaj7 V7', '♭VImaj7 ♭VII ♭VImaj7 V7'),
  T('Drop to iv', 'Pulls the energy down so the last chorus can come back bigger.', 45, 45, ['melancholic', 'bittersweet', 'nostalgic', 'chill'], 'iv9:8 VImaj7:8', 'IVmaj7:8 iv6:8'),
  T('Climb back up', 'Steps up through the key one bar at a time and parks on V.', 65, 60, ['triumphant', 'hopeful', 'euphoric', 'soulful'], 'VI VII i V7', 'IV V vi V7'),
];
const VAMP: Template[] = [
  T('Two-chord vamp', 'Repeats two chords so the singer can ad-lib over the top.', 75, 40, ['soulful', 'romantic', 'chill'], 'i9:8 IV9:8', 'IVmaj7:8 Imaj7:8'),
  T('Climbing vamp', 'Walks the bass up each bar for a church-style climb.', 85, 55, ['soulful', 'triumphant', 'hopeful'], 'i7 iiø7 IIImaj7 iv7', 'I ii7 iii7 IV'),
  T('Shout chords', 'Hits the dominant every other bar for a shout chorus feel.', 90, 70, ['triumphant', 'euphoric', 'aggressive'], 'i:8 V7:8', 'I:8 V7:8'),
];
const OUTRO: Template[] = [
  T('Fade on home', 'Returns to the first chord of your idea and lets it ring.', 20, 10, ['chill', 'dreamy', 'romantic'], 'i9:16', 'Imaj7:16'),
  T('Leave it open', 'Ends away from home, so the song hangs in the air.', 25, 40, ['melancholic', 'mysterious', 'bittersweet', 'cinematic', 'dark'], 'i9:8 VImaj7:8', 'Imaj7:8 vi7:8'),
  T('Major finish', 'Lands on a major home chord for a warm, final lift.', 30, 20, ['hopeful', 'triumphant', 'soulful'], 'VImaj7:8 I:8', 'IVmaj7:8 Iadd9:8'),
];

const POOLS: Record<SectionKind, Template[]> = {
  intro: INTRO, verse: VERSE, pre: PRE, chorus: CHORUS, hook: CHORUS, post: CHORUS, bridge: BRIDGE, build: PRE,
  drop: CHORUS, breakdown: VERSE, vamp: VAMP, outro: OUTRO, loop: VERSE,
};

function rng(seed: number) {
  let s = seed >>> 0 || 0x9e3779b9;
  return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 2 ** 32);
}

function hash(str: string) {
  let h = 2166136261;
  for (let i = 0; i < str.length; i++) h = Math.imul(h ^ str.charCodeAt(i), 16777619) >>> 0;
  return h;
}

function realize(prog: string, key: ReturnType<typeof parseKey>, beats: number): SongChord[] {
  const table = key.minor ? MINOR : MAJOR;
  const toks = prog.split(/\s+/).filter(Boolean).map((t) => {
    const [roman, len] = t.split(':');
    return { roman, len: Number(len) || 4 };
  });
  const out: SongChord[] = [];
  let at = 0;
  for (let k = 0; at < beats && k < 256; k++) {
    const t = toks[k % toks.length];
    const [off, q, fn] = table[t.roman] ?? [0, key.minor ? 'm' : '', 'tonic'];
    const len = Math.min(t.len, beats - at);
    out.push({ startBeat: at, beats: len, symbol: key.names[(key.pc + off) % 12] + q, roman: t.roman, fn });
    at += len;
  }
  return out;
}

function enrich(symbol: string): string {
  if (symbol.includes('/')) return symbol;
  const m = /^([A-G][♭♯]?)(.*)$/.exec(symbol);
  if (!m) return symbol;
  const [, root, q] = m;
  const up: Record<string, string> = { '': 'add9', m: 'm9', m7: 'm9', maj7: 'maj9', '7': '9', sus4: '7sus4' };
  return root + (up[q] ?? q);
}

function ideaOptions(analysis: AnalysisResult | undefined, bars: number, seed: number, key: ReturnType<typeof parseKey>): SectionOption[] {
  const beats = bars * (analysis?.beatsPerBar ?? 4);
  if (!analysis) {
    const base = realize('i9 VImaj7 IIImaj7 VII', key, beats);
    return [{ title: 'As described', explanation: 'A starting loop from your description.', energy: 50, tension: 35, chords: base }];
  }
  const heard: SongChord[] = analysis.segments
    .filter((s) => s.startBeat < beats)
    .map((s) => ({ startBeat: s.startBeat, beats: Math.min(s.beats, beats - s.startBeat), symbol: s.symbol, roman: s.roman, fn: s.fn }));
  const sugg = mockSuggestions(analysis, seed)[0];
  const reharm: SongChord[] = (sugg?.chords ?? [])
    .filter((c) => c.startBeat < beats)
    .map((c) => ({ startBeat: c.startBeat, beats: Math.min(c.beats, beats - c.startBeat), symbol: c.symbol, roman: c.roman, fn: c.fn }));
  return [
    { title: 'As you played it', explanation: 'Your chords exactly as heard, so this section sounds like your idea.', energy: 55, tension: 35, chords: heard },
    { title: 'Richer voicings', explanation: 'The same changes with added 9ths and 7ths for a fuller, warmer sound.', energy: 60, tension: 40, chords: heard.map((c) => ({ ...c, symbol: enrich(c.symbol) })) },
    { title: sugg ? sugg.title : 'Light reharm', explanation: sugg ? sugg.summary : 'One borrowed chord to freshen the turnaround.', energy: 60, tension: sugg?.tension ?? 45, chords: reharm.length ? reharm : heard },
  ];
}

/** Deterministic mock of the C++ song builder: same request and analysis give the same song. */
export function mockSong(req: BuildSongRequest, analysis?: AnalysisResult): Song {
  const structure = resolveStructure(req);
  const { moods } = readDescription(req.description, req.moods);
  const keyName = analysis?.key ?? 'F minor';
  const key = parseKey(keyName);
  const beatsPerBar = analysis?.beatsPerBar ?? 4;
  const form = FORMS[structure];
  const ideaKind = IDEA_KINDS[req.ideaSection].find((k) => form.some(([f]) => f === k)) ?? form[1][0];
  const ideaBars = Math.min(8, analysis?.bars ?? 8);

  const counts = new Map<SectionKind, number>();
  for (const [k] of form) counts.set(k, (counts.get(k) ?? 0) + 1);
  const seen = new Map<SectionKind, number>();

  const optionsByKind = new Map<string, SectionOption[]>();
  const optionsFor = (kind: SectionKind, bars: number, isIdea: boolean): SectionOption[] => {
    const memo = `${kind}:${bars}:${isIdea}`;
    const hit = optionsByKind.get(memo);
    if (hit) return hit;
    let opts: SectionOption[];
    if (isIdea) {
      // The idea keeps its own length; longer sections loop it.
      const base = ideaOptions(analysis, ideaBars, req.seed, key);
      opts = base.map((o) => ({ ...o, chords: loop(o.chords, ideaBars * beatsPerBar, bars * beatsPerBar) }));
    } else {
      const r = rng(hash(`${req.seed}|${kind}|${moods.join(',')}`));
      const pool = POOLS[kind].map((t) => ({ t, score: t.tags.filter((m) => moods.includes(m)).length * 10 + r() * 8 }));
      pool.sort((a, b) => b.score - a.score);
      opts = pool.slice(0, 3).map(({ t }) => ({
        title: t.title, explanation: t.explanation, energy: t.energy, tension: t.tension,
        chords: realize(key.minor ? t.minor : t.major, key, bars * beatsPerBar),
      }));
    }
    optionsByKind.set(memo, opts);
    return opts;
  };

  let bar = 0;
  const sections: SongSection[] = form.map(([kind, bars]) => {
    const n = (seen.get(kind) ?? 0) + 1;
    seen.set(kind, n);
    const isIdea = kind === ideaKind;
    const label = (counts.get(kind) ?? 1) > 1 ? `${KIND_LABEL[kind]} ${n}` : KIND_LABEL[kind];
    const sec: SongSection = { section: kind, label, startBar: bar, bars, chosen: 0, isIdea, options: optionsFor(kind, bars, isIdea) };
    bar += bars;
    return sec;
  });

  const moodWords = moods.slice(0, 2).map((m) => MOOD_LABEL[m].toLowerCase());
  const interpretedAs = `${moodWords.length ? `${cap(moodWords.join(', '))} ` : ''}${STRUCTURE_LABEL[structure]} with your idea as the ${IDEA_LABEL[req.ideaSection].toLowerCase()}`;

  return {
    schemaVersion: SCHEMA_VERSION,
    id: `song-${(hash(JSON.stringify([req.description, req.moods, req.ideaSection, structure])) ^ req.seed).toString(16)}-${req.seed}`,
    key: keyName,
    bpm: analysis?.bpm ?? 92,
    beatsPerBar,
    structure,
    moods,
    interpreter: 'offline',
    interpretedAs,
    arc: ARC[structure],
    sections,
  };
}

const cap = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);

function loop(chords: SongChord[], period: number, total: number): SongChord[] {
  if (period >= total) return chords.filter((c) => c.startBeat < total).map((c) => ({ ...c, beats: Math.min(c.beats, total - c.startBeat) }));
  const out: SongChord[] = [];
  for (let off = 0; off < total; off += period)
    for (const c of chords) if (off + c.startBeat < total) out.push({ ...c, startBeat: off + c.startBeat, beats: Math.min(c.beats, total - off - c.startBeat) });
  return out;
}

export const defaultBuildRequest = (seed = 0): BuildSongRequest => ({
  schemaVersion: SCHEMA_VERSION,
  description: 'Dark late-night R&B, hopeful chorus',
  moods: ['dark', 'hopeful'],
  ideaSection: 'verse',
  structure: 'auto',
  seed,
  useOnlineAi: false,
});
