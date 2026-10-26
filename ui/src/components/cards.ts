import type { Store } from '../state/store';
import type { Suggestion } from '../state/types';
import { chordHtml } from '../theory/chords';
import { el, esc, icon, tensionColor } from '../ui/dom';

export interface CardActions {
  select(id: string): void;
  toggleFavorite(id: string): void;
  audition(id: string): void;
}

const TECH_HINT: Record<string, string> = {
  Diatonic: 'Chords from the key itself',
  Borrowed: 'Chords borrowed from the parallel major or minor (modal interchange)',
  Secondary: 'Dominants that point at a chord other than the tonic',
  'Tritone sub': 'A dominant replaced by the one a tritone away',
  'Chromatic mediant': 'Chords a third away with a surprising shared tone',
  'Neo-Riemannian': 'One-note moves between major and minor triads (P, L, R)',
};

function movementLabel(m: number) {
  return m < 1.5 ? 'Smooth' : m < 2.5 ? 'Moderate' : 'Leapy';
}

/** The 4 bars around the first change, so every symbol stays readable on a small card. */
function miniWindow(sg: Suggestion) {
  const first = sg.chords.find((c) => c.changed);
  const start = first ? Math.floor(first.startBeat / 16) * 16 : 0;
  return { start, end: start + 16 };
}

function miniStrip(sg: Suggestion, s: ReturnType<Store['get']>) {
  const { start, end } = miniWindow(sg);
  return sg.chords
    .filter((c) => c.startBeat >= start && c.startBeat < end)
    .map((c) => `<span class="mini${c.changed ? ' is-changed' : ''}${c.beats < 4 ? ' is-short' : ''}" style="flex:${c.beats}">${chordHtml(c.symbol, s.settings.notation)}</span>`)
    .join('');
}

export function SuggestionGrid(store: Store, actions: CardActions) {
  const root = el('section', { class: 'grid-wrap', 'aria-label': 'Suggestions' });
  root.innerHTML = `
    <div class="grid-head">
      <h2>Suggestions</h2>
      <span class="grid-count" data-slot="count"></span>
      <span class="grid-hint">Arrow keys to browse, Space to listen</span>
    </div>
    <div class="grid" role="listbox" aria-label="Reharmonization suggestions" data-slot="grid"></div>`;

  const grid = root.querySelector<HTMLElement>('[data-slot=grid]')!;
  const count = root.querySelector<HTMLElement>('[data-slot=count]')!;

  grid.addEventListener('click', (e) => {
    const t = e.target as HTMLElement;
    const card = t.closest<HTMLElement>('[data-card]');
    if (!card) return;
    const id = card.dataset.card!;
    if (t.closest('[data-act=star]')) return actions.toggleFavorite(id);
    if (t.closest('[data-act=play]')) return actions.audition(id);
    actions.select(id);
  });

  function render() {
    const s = store.get();
    count.textContent = s.generating ? 'Writing…' : `${s.suggestions.length}`;
    grid.classList.toggle('is-generating', s.generating);
    grid.innerHTML = s.suggestions
      .map((sg) => {
        const selected = sg.id === s.selectedSuggestionId;
        const playing = selected && s.auditioning && s.abMode === 'suggestion';
        const fav = s.favorites.has(sg.id);
        const changedBars = new Set(sg.chords.filter((c) => c.changed && c.startBeat < 32).map((c) => Math.floor(c.startBeat / 4) + 1));
        const hasLocked = s.locked.size > 0;
        return `<article class="card${selected ? ' is-selected' : ''}${playing ? ' is-playing' : ''}${fav ? ' is-fav' : ''}"
              role="option" aria-selected="${selected}" tabindex="${selected ? 0 : -1}" data-card="${sg.id}">
            <header class="card-head">
              <h3 title="${esc(sg.title)}">${esc(sg.title)}</h3>
              <button class="icon-btn sm star${fav ? ' on' : ''}" data-act="star" aria-pressed="${fav}" aria-label="${fav ? 'Remove from' : 'Add to'} favorites">${icon.star(fav)}</button>
            </header>
            <div class="mini-wrap"><span class="mini-bars num" aria-hidden="true">${miniWindow(sg).start / 4 + 1}</span><div class="mini-strip" aria-hidden="true">${miniStrip(sg, s)}<span class="mini-playhead"></span></div></div>
            <p class="card-summary">${esc(sg.summary)}</p>
            <div class="card-meta">
              <span class="tags">${sg.techniques.map((t) => `<span class="tag" title="${esc(TECH_HINT[t] ?? '')}">${esc(t)}</span>`).join('')}</span>
              <span class="card-bars num" title="Bars changed in each 8-bar phrase">${changedBars.size ? `${changedBars.size > 1 ? 'bars' : 'bar'} ${[...changedBars].join(', ')}` : ''}</span>
            </div>
            <footer class="card-foot">
              <span class="card-stats">
                <span class="tension-meter" title="Tension ${sg.tension} of 100" style="--t:${sg.tension}%;--tc:${tensionColor(sg.tension / 100)}"><i></i></span>
                <span class="num tension-num">${sg.tension}</span>
                <span class="move" title="Average voice movement: ${sg.movement.toFixed(1)} semitones per change">${movementLabel(sg.movement)}</span>
              </span>
              <button class="play-btn" data-act="play" aria-label="${playing ? 'Stop' : 'Listen to'} ${esc(sg.title)}">${playing ? icon.pause(12) : icon.play(12)}</button>
            </footer>
            ${hasLocked ? `<span class="badge-locked" title="Some bars are locked and stay as heard">${icon.lock()} locked bars kept</span>` : ''}
          </article>`;
      })
      .join('');
  }

  store.subscribe((_, changed) => {
    if (['suggestions', 'selectedSuggestionId', 'favorites', 'auditioning', 'abMode', 'generating', 'locked', 'settings'].some((k) => changed.has(k as never))) render();
  });
  render();
  return root;
}
