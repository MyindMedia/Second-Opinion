import type { State, Store } from '../state/store';
import type { HarmonicFunction, MelodyNote } from '../state/types';
import { chordHtml, isParsed, melodyRelation, parseChord, pcNameFlat, voice, type ParsedChord } from '../theory/chords';
import { el, esc, icon } from '../ui/dom';

export interface WhyActions {
  setDepth(d: 'simple' | 'detailed'): void;
  audition(): void;
  setAB(mode: 'original' | 'suggestion'): void;
  closeDrawer(): void;
}

const FN_NAME: Record<HarmonicFunction, string> = { tonic: 'Tonic', predominant: 'Predominant', dominant: 'Dominant', color: 'Color' };

const FN_PLAIN: Record<HarmonicFunction, string> = {
  tonic: 'Home base: the phrase can rest here.',
  predominant: 'Builds motion away from home and sets up the dominant.',
  dominant: 'Creates pull back toward the tonic.',
  color: 'Adds color outside the key.',
};

interface Focus {
  symbol: string;
  roman: string;
  fn: HarmonicFunction;
  startBeat: number;
  beats: number;
  plain: string;
  term: string;
  detail?: string;
  prevSymbol?: string;
  lane: 'original' | 'suggestion';
  confidence?: number;
}

function resolveFocus(s: State): Focus | null {
  const a = s.analysis;
  if (!a || !s.focusedChord) return null;
  if (s.focusedChord.lane === 'original') {
    const i = a.segments.findIndex((x) => x.id === s.focusedChord!.id);
    if (i < 0) return null;
    const seg = a.segments[i];
    return {
      symbol: seg.symbol, roman: seg.roman, fn: seg.fn, startBeat: seg.startBeat, beats: seg.beats,
      plain: FN_PLAIN[seg.fn], term: `${seg.roman}, ${FN_NAME[seg.fn].toLowerCase()}`,
      prevSymbol: a.segments[i - 1]?.symbol, lane: 'original', confidence: seg.confidence,
    };
  }
  const sg = s.suggestions.find((x) => x.id === s.selectedSuggestionId);
  const i = sg?.chords.findIndex((x) => x.id === s.focusedChord!.id) ?? -1;
  if (!sg || i < 0) return null;
  const c = sg.chords[i];
  return {
    symbol: c.symbol, roman: c.roman, fn: c.fn, startBeat: c.startBeat, beats: c.beats,
    plain: c.why?.plain ?? (c.changed ? FN_PLAIN[c.fn] : 'Same as what you played. Kept so the change around it stands out.'),
    term: c.why?.term ?? `${c.roman}, ${FN_NAME[c.fn].toLowerCase()}`,
    detail: c.why?.detail, prevSymbol: sg.chords[i - 1]?.symbol, lane: 'suggestion',
  };
}

function melodyAt(melody: MelodyNote[], start: number, beats: number) {
  return melody.find((n) => n.startBeat >= start && n.startBeat < start + beats) ?? melody.find((n) => n.startBeat <= start && n.startBeat + n.beats > start);
}

function voiceLeadingSvg(prev: ParsedChord | null, cur: ParsedChord) {
  const b = prev ? voice(prev) : [];
  const v = voice(cur, b.length ? b : undefined);
  const all = [...b, ...v];
  const lo = Math.min(...all) - 1, hi = Math.max(...all) + 1;
  const H = 108, W = 248, x0 = 54, x1 = W - 54;
  const y = (n: number) => 8 + (1 - (n - lo) / (hi - lo)) * (H - 16);
  let total = 0;
  const lines = v
    .map((n, i) => {
      if (!b.length) return '';
      const p = b[Math.min(i, b.length - 1)];
      total += Math.abs(n - p);
      const held = p === n;
      return `<line x1="${x0 + 6}" y1="${y(p)}" x2="${x1 - 6}" y2="${y(n)}" class="${held ? 'vl-hold' : 'vl-move'}"/>`;
    })
    .join('');
  const dots = (notes: number[], x: number, cls: string, labelLeft: boolean) =>
    notes.map((n) => `<circle cx="${x}" cy="${y(n)}" r="4.5" class="${cls}"/><text x="${labelLeft ? x - 12 : x + 12}" y="${y(n) + 3.5}" text-anchor="${labelLeft ? 'end' : 'start'}" class="vl-label">${pcNameFlat(n)}</text>`).join('');
  return {
    svg: `<svg class="vl" viewBox="0 0 ${W} ${H}" width="100%" height="${H}" role="img" aria-label="Voice-leading from the previous chord">
      ${lines}${b.length ? dots(b, x0, 'vl-prev', true) : ''}${dots(v, x1, 'vl-cur', false)}</svg>`,
    total,
  };
}

export function WhyPanel(store: Store, actions: WhyActions) {
  const root = el('aside', { class: 'why', 'aria-label': 'Why this works' });

  root.addEventListener('click', (e) => {
    const t = (e.target as HTMLElement).closest<HTMLElement>('[data-act]');
    if (!t) return;
    const act = t.dataset.act!;
    if (act === 'simple' || act === 'detailed') actions.setDepth(act);
    if (act === 'listen') actions.audition();
    if (act === 'ab-original') actions.setAB('original');
    if (act === 'ab-suggestion') actions.setAB('suggestion');
    if (act === 'close') actions.closeDrawer();
  });

  function render() {
    const s = store.get();
    const f = resolveFocus(s);
    const depth = s.settings.depth;
    const sugg = s.suggestions.find((x) => x.id === s.selectedSuggestionId);

    const head = `
      <div class="why-head">
        <h2>Why this works</h2>
        <div class="seg-ctl sm" role="radiogroup" aria-label="Explanation depth">
          <button role="radio" aria-checked="${depth === 'simple'}" data-act="simple">Simple</button>
          <button role="radio" aria-checked="${depth === 'detailed'}" data-act="detailed">Detailed</button>
        </div>
        <button class="icon-btn sm only-compact" data-act="close" aria-label="Close">${icon.close()}</button>
      </div>`;

    const listen = `
      <div class="why-listen">
        <button class="btn btn-ghost${s.auditioning ? ' is-on' : ''}" data-act="listen" ${sugg ? '' : 'disabled'}>
          ${s.auditioning ? icon.pause(12) : icon.play(12)}<span>${s.auditioning ? 'Stop' : 'Listen'}</span><kbd>Space</kbd>
        </button>
        <div class="seg-ctl ab" role="radiogroup" aria-label="Compare original and suggestion">
          <button role="radio" aria-checked="${s.abMode === 'original'}" data-act="ab-original"><i class="sw sw-original"></i>Original</button>
          <button role="radio" aria-checked="${s.abMode === 'suggestion'}" data-act="ab-suggestion" ${sugg ? '' : 'disabled'}><i class="sw sw-suggestion"></i>Suggestion</button>
        </div>
      </div>`;

    if (!f || !s.analysis) {
      root.innerHTML = `${head}<div class="why-empty"><p>Select a chord or suggestion to see why it works.</p></div>${listen}`;
      return;
    }

    const parsed = parseChord(f.symbol);
    const prevParsed = f.prevSymbol ? parseChord(f.prevSymbol) : null;
    const mel = melodyAt(s.analysis.melody, f.startBeat, f.beats);
    let melodyLine = '';
    let vl = '';
    if (isParsed(parsed)) {
      if (mel) {
        const rel = melodyRelation(mel.pitch % 12, parsed);
        melodyLine = `<p class="why-melody"><span class="melody-note num">${pcNameFlat(mel.pitch)}</span> Melody note ${pcNameFlat(mel.pitch)} is the ${rel} here.</p>`;
      }
      const diagram = voiceLeadingSvg(prevParsed && isParsed(prevParsed) ? prevParsed : null, parsed);
      vl = `
        <figure class="why-vl">
          <figcaption>
            <span>${prevParsed && isParsed(prevParsed) ? `From ${chordHtml(f.prevSymbol!, s.settings.notation)}` : 'Voicing'}</span>
            <span class="num">${prevParsed && isParsed(prevParsed) ? `${diagram.total} semitones moved` : ''}</span>
          </figcaption>
          ${diagram.svg}
        </figure>`;
    }

    const conf = f.confidence !== undefined
      ? `<p class="why-conf">Heard with <span class="num">${Math.round(f.confidence * 100)}%</span> confidence${f.confidence < 0.5 ? '. Click the chord to confirm or correct it.' : '.'}</p>`
      : '';

    root.innerHTML = `${head}
      <div class="why-body">
        <div class="why-id">
          <div class="why-roman">${esc(f.roman)}</div>
          <div class="why-chord">
            <span class="why-symbol">${chordHtml(f.symbol, s.settings.notation)}</span>
            <span class="fn-chip fn-${f.fn}"><i></i>${FN_NAME[f.fn]}</span>
          </div>
        </div>
        <p class="why-plain">${esc(f.plain)} <span class="why-term">(${esc(f.term)})</span></p>
        ${melodyLine}
        ${conf}
        ${depth === 'detailed' && f.detail ? `<p class="why-detail">${esc(f.detail)}</p>` : ''}
        ${depth === 'detailed' || f.lane === 'suggestion' ? vl : ''}
      </div>
      ${listen}`;
  }

  store.subscribe((_, changed) => {
    if (['focusedChord', 'selectedSuggestionId', 'settings', 'auditioning', 'abMode', 'analysis', 'suggestions'].some((k) => changed.has(k as never))) render();
  });
  render();
  return root;
}
