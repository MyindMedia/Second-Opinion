import type { Store } from '../state/store';
import type { ChordSegment, HarmonicFunction, SuggestionChord } from '../state/types';
import { chordHtml } from '../theory/chords';
import { clamp, el, esc, icon } from '../ui/dom';

export interface TimelineActions {
  selectOriginal(id: string, anchor: HTMLElement): void;
  selectSuggestionChord(id: string): void;
  setRange(startBeat: number, endBeat: number): void;
}

const FN_LABEL: Record<HarmonicFunction, string> = { tonic: 'Tonic', predominant: 'Predominant', dominant: 'Dominant', color: 'Color' };

function confLevel(c: number) {
  return c >= 0.75 ? 'high' : c >= 0.5 ? 'mid' : 'low';
}

function confDots(c: number) {
  const n = c >= 0.75 ? 3 : c >= 0.5 ? 2 : 1;
  return `<span class="conf-dots" aria-hidden="true">${[0, 1, 2].map((i) => `<i class="${i < n ? 'on' : ''}"></i>`).join('')}</span>`;
}

export function Timeline(store: Store, actions: TimelineActions) {
  const root = el('section', { class: 'timeline', 'aria-label': 'Chord timeline' });
  root.innerHTML = `
    <div class="tl-gutter">
      <div class="tl-zoom" role="group" aria-label="Zoom">
        <button class="icon-btn sm" data-zoom="-1" aria-label="Zoom out">${icon.minus()}</button>
        <button class="icon-btn sm" data-zoom="1" aria-label="Zoom in">${icon.plus()}</button>
      </div>
      <div class="tl-label lane-original"><i></i>Heard</div>
      <div class="tl-label lane-suggestion"><i></i><span data-slot="sugg-label">Suggestion</span></div>
      <div class="tl-label">Function</div>
      <div class="tl-label">Melody</div>
    </div>
    <div class="tl-scroll" tabindex="-1">
      <div class="tl-content">
        <div class="tl-ruler" data-slot="ruler"></div>
        <div class="tl-lane tl-original" data-slot="original" role="list" aria-label="Detected chords"></div>
        <div class="tl-lane tl-suggestion" data-slot="suggestion" role="list" aria-label="Suggested chords"></div>
        <div class="tl-roman" data-slot="roman"></div>
        <div class="tl-melody" data-slot="melody" aria-hidden="true"></div>
        <div class="tl-selection" data-slot="selection" hidden></div>
        <div class="tl-playhead" data-slot="playhead" hidden></div>
      </div>
    </div>`;

  const q = <T extends HTMLElement>(s: string) => root.querySelector<T>(`[data-slot=${s}]`)!;
  const scroll = root.querySelector<HTMLElement>('.tl-scroll')!;
  const content = root.querySelector<HTMLElement>('.tl-content')!;
  const ruler = q('ruler'), original = q('original'), suggestion = q('suggestion'), roman = q('roman'), melody = q('melody');
  const selection = q('selection'), playhead = q('playhead'), suggLabel = q('sugg-label');

  let range: [number, number] | null = null;

  root.addEventListener('click', (e) => {
    const t = e.target as HTMLElement;
    const zoom = t.closest<HTMLElement>('[data-zoom]');
    if (zoom) {
      const s = store.get();
      store.set({ pxPerBeat: clamp(s.pxPerBeat * (zoom.dataset.zoom === '1' ? 1.25 : 0.8), 8, 48) });
      return;
    }
    const seg = t.closest<HTMLElement>('[data-seg]');
    if (seg?.dataset.lane === 'original') actions.selectOriginal(seg.dataset.seg!, seg);
    if (seg?.dataset.lane === 'suggestion') actions.selectSuggestionChord(seg.dataset.seg!);
  });

  scroll.addEventListener('wheel', (e) => {
    if (!(e.ctrlKey || e.metaKey)) return;
    e.preventDefault();
    store.set({ pxPerBeat: clamp(store.get().pxPerBeat * (e.deltaY < 0 ? 1.1 : 0.9), 8, 48) });
  }, { passive: false });

  // Drag across the ruler to select a range of beats (Lock and Regenerate act on it).
  ruler.addEventListener('pointerdown', (e) => {
    const s = store.get();
    const rect = content.getBoundingClientRect();
    const beatAt = (x: number) => Math.round(clamp((x - rect.left) / s.pxPerBeat, 0, (s.analysis?.bars ?? 32) * 4));
    const start = beatAt(e.clientX);
    ruler.setPointerCapture(e.pointerId);
    const move = (ev: PointerEvent) => {
      const b = beatAt(ev.clientX);
      range = [Math.min(start, b), Math.max(start, b)];
      drawSelection();
    };
    const up = (ev: PointerEvent) => {
      ruler.removeEventListener('pointermove', move);
      ruler.removeEventListener('pointerup', up);
      const b = beatAt(ev.clientX);
      if (Math.abs(b - start) < 1) range = null;
      drawSelection();
      if (range) actions.setRange(range[0], range[1]);
      else actions.setRange(0, 0);
    };
    ruler.addEventListener('pointermove', move);
    ruler.addEventListener('pointerup', up);
  });

  function drawSelection() {
    const ppb = store.get().pxPerBeat;
    selection.hidden = !range;
    if (range) {
      selection.style.left = `${range[0] * ppb}px`;
      selection.style.width = `${(range[1] - range[0]) * ppb}px`;
    }
  }

  function segmentHtml(seg: ChordSegment, ppb: number, s: ReturnType<Store['get']>) {
    const width = seg.beats * ppb - 3;
    const level = confLevel(seg.confidence);
    const locked = s.locked.has(seg.id);
    const selected = s.focusedChord?.lane === 'original' && s.focusedChord.id === seg.id;
    const pct = Math.round(seg.confidence * 100);
    const narrow = width < 54;
    return `<button role="listitem" class="seg seg-original conf-${level}${locked ? ' is-locked' : ''}${seg.userCorrected ? ' is-corrected' : ''}${selected ? ' is-selected' : ''}${narrow ? ' is-narrow' : ''}"
        style="left:${seg.startBeat * ppb}px;width:${width}px" data-seg="${seg.id}" data-lane="original"
        title="${esc(seg.symbol)}, ${pct}% sure${level === 'low' ? '. Click to confirm' : ''}"
        aria-label="${esc(seg.symbol)}, bar ${Math.floor(seg.startBeat / 4) + 1}, ${pct} percent confidence${locked ? ', locked' : ''}">
        ${chordHtml(seg.symbol, s.settings.notation)}
        ${level === 'low' ? '<span class="unsure" aria-hidden="true">?</span>' : ''}
        <span class="seg-meta">
          ${locked ? `<span class="seg-icon">${icon.lock()}</span>` : ''}
          ${seg.userCorrected ? `<span class="seg-icon">${icon.pencil()}</span>` : ''}
          ${narrow ? '' : confDots(seg.confidence)}
        </span>
        <span class="seg-pct num" aria-hidden="true">${pct}%</span>
      </button>`;
  }

  function suggestionHtml(c: SuggestionChord, ppb: number, s: ReturnType<Store['get']>) {
    const width = c.beats * ppb - 3;
    const selected = s.focusedChord?.lane === 'suggestion' && s.focusedChord.id === c.id;
    return `<button role="listitem" class="seg seg-suggestion${c.changed ? ' is-changed' : ' is-same'}${selected ? ' is-selected' : ''}${width < 54 ? ' is-narrow' : ''}"
        style="left:${c.startBeat * ppb}px;width:${width}px" data-seg="${c.id}" data-lane="suggestion"
        aria-label="${esc(c.symbol)}${c.changed ? ', changed' : ', same as heard'}">
        ${chordHtml(c.symbol, s.settings.notation)}
      </button>`;
  }

  function render() {
    const s = store.get();
    const a = s.analysis;
    if (!a) return;
    const ppb = s.pxPerBeat;
    const totalBeats = a.bars * a.beatsPerBar;
    content.style.width = `${totalBeats * ppb + 24}px`;
    content.style.setProperty('--ppb', `${ppb}px`);
    content.style.setProperty('--ppbar', `${ppb * a.beatsPerBar}px`);
    content.style.setProperty('--chord-fs', ppb >= 18 ? '20px' : ppb >= 13 ? '17px' : '14px');

    const every = ppb * 4 < 40 ? 4 : ppb * 4 < 72 ? 2 : 1;
    let r = '';
    for (let bar = 0; bar < a.bars; bar++) {
      if (bar % every === 0) r += `<span class="bar-num num" style="left:${bar * ppb * 4}px">${bar + 1}</span>`;
    }
    ruler.innerHTML = r;

    original.innerHTML = a.segments.map((seg) => segmentHtml(seg, ppb, s)).join('');

    const sugg = s.suggestions.find((x) => x.id === s.selectedSuggestionId);
    suggLabel.textContent = sugg ? 'Suggestion' : 'Suggestion';
    suggestion.innerHTML = sugg
      ? sugg.chords.map((c) => suggestionHtml(c, ppb, s)).join('')
      : `<span class="lane-empty">Pick a suggestion below to see it under your chords</span>`;

    const rows: { startBeat: number; beats: number; roman: string; fn: HarmonicFunction }[] = sugg ? sugg.chords : a.segments;
    roman.innerHTML = rows
      .map((x) => `<span class="rn fn-${x.fn}" style="left:${x.startBeat * ppb}px;width:${x.beats * ppb - 3}px" title="${FN_LABEL[x.fn]}"><i aria-hidden="true"></i><b>${esc(x.roman)}</b></span>`)
      .join('');

    const lo = 60, hi = 78;
    melody.innerHTML = a.melody
      .map((n) => {
        const y = 1 - (clamp(n.pitch, lo, hi) - lo) / (hi - lo);
        return `<i style="left:${n.startBeat * ppb}px;width:${Math.max(3, n.beats * ppb - 2)}px;top:calc(${(y * 100).toFixed(1)}% - ${(y * 4).toFixed(1)}px)"></i>`;
      })
      .join('');

    drawSelection();
  }

  function renderPlayhead() {
    const s = store.get();
    const beat = s.playheadBeat;
    playhead.hidden = beat === null;
    if (beat !== null) {
      const x = beat * s.pxPerBeat;
      playhead.style.transform = `translateX(${x}px)`;
      // Keep the playhead in view while auditioning.
      if (x < scroll.scrollLeft + 40 || x > scroll.scrollLeft + scroll.clientWidth - 80) scroll.scrollLeft = Math.max(0, x - 80);
    }
  }

  store.subscribe((_, changed) => {
    if (['analysis', 'suggestions', 'selectedSuggestionId', 'focusedChord', 'locked', 'pxPerBeat', 'settings'].some((k) => changed.has(k as never))) render();
    if (changed.has('playheadBeat') || changed.has('pxPerBeat')) renderPlayhead();
  });

  render();
  return root;
}
