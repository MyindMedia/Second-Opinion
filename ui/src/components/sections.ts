// Build out workspace: SectionCards for the selected section (3 options), plus the empty,
// loading ("Writing your song…") and error states.
import type { State, Store } from '../state/store';
import { sectionChords, sectionRange } from '../state/song';
import { chordHtml } from '../theory/chords';
import { el, esc, icon, tensionColor } from '../ui/dom';

export interface SectionActions {
  selectSection(index: number): void;
  focusOption(index: number): void;
  useOption(index: number): void;
  audition(optionIndex: number): void;
  setScope(scope: 'section' | 'song'): void;
  retry(): void;
  cancel(): void;
}

function strip(s: State, optionIndex: number) {
  const song = s.song!;
  const sec = song.sections[s.selectedSection];
  const chords = sectionChords(song, s.selectedSection, optionIndex);
  const start = sec.startBar * song.beatsPerBar;
  // Up to 8 bars; denser options show 4 so every symbol stays readable on a card.
  const dense = chords.filter((c) => c.startBeat < start + 32).length > 6;
  const span = Math.min(sec.bars * song.beatsPerBar, dense ? 16 : 32);
  const shown = chords.filter((c) => c.startBeat < start + span);
  return {
    html: shown.map((c) => {
      const beats = Math.min(c.beats, start + span - c.startBeat);
      return `<span class="mini${sec.isIdea && optionIndex === 0 ? ' is-heard' : ' is-changed'}${beats < 4 ? ' is-short' : ''}" style="flex:${beats}" title="${esc(c.symbol)}, ${esc(c.roman)}">${chordHtml(c.symbol, s.settings.notation)}</span>`;
    }).join(''),
  };
}

function meter(label: string, v: number, cls: string, color: string) {
  return `<span class="opt-meter ${cls}" title="${label} ${v} of 100">
      <span class="opt-meter-label">${label}</span>
      <span class="opt-bar" style="--v:${v}%;--c:${color}"><i></i></span>
      <span class="num">${v}</span>
    </span>`;
}

export function SongWorkspace(store: Store, actions: SectionActions) {
  const root = el('section', { class: 'song-wrap', 'aria-label': 'Song sections' });

  root.addEventListener('click', (e) => {
    const t = e.target as HTMLElement;
    const act = t.closest<HTMLElement>('[data-act]')?.dataset.act;
    const card = t.closest<HTMLElement>('[data-opt]');
    const i = card ? Number(card.dataset.opt) : -1;
    if (act === 'use' && i >= 0) return actions.useOption(i);
    if (act === 'play' && i >= 0) return actions.audition(i);
    if (act === 'prev') return actions.selectSection(store.get().selectedSection - 1);
    if (act === 'next') return actions.selectSection(store.get().selectedSection + 1);
    if (act === 'scope-section') return actions.setScope('section');
    if (act === 'scope-song') return actions.setScope('song');
    if (act === 'retry') return actions.retry();
    if (act === 'cancel') return actions.cancel();
    if (card) actions.focusOption(i);
  });

  root.addEventListener('keydown', (e) => {
    const card = (e.target as HTMLElement).closest<HTMLElement>('[data-opt]');
    if (!card || e.target !== card) return;
    const i = Number(card.dataset.opt);
    const n = store.get().song?.sections[store.get().selectedSection]?.options.length ?? 0;
    if (e.key === 'Enter') { e.preventDefault(); actions.useOption(i); }
    if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
      e.preventDefault();
      e.stopPropagation();
      const j = (i + (e.key === 'ArrowRight' ? 1 : n - 1)) % n;
      actions.focusOption(j);
      requestAnimationFrame(() => root.querySelector<HTMLElement>(`[data-opt="${j}"]`)?.focus());
    }
  });

  function render() {
    const s = store.get();
    root.hidden = s.mode !== 'build';
    if (s.mode !== 'build') return;

    if (s.songStatus === 'building') {
      const existing = root.querySelector<HTMLElement>('.song-loading');
      if (existing) {
        // Progress ticks only move the bar, so the shimmer and focus stay put.
        existing.setAttribute('aria-valuenow', String(Math.round(s.songProgress)));
        existing.querySelector<HTMLElement>('.stage-bar i')!.style.width = `${s.songProgress}%`;
        return;
      }
      const online = s.settings.useOnlineAi && s.aiStatus.hasKey;
      root.innerHTML = `
        <div class="song-state song-loading" role="progressbar" aria-label="Writing your song" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${Math.round(s.songProgress)}">
          <h2>Writing your song…</h2>
          <span class="stage-bar is-active"><i style="width:${s.songProgress}%"></i></span>
          <p>${online ? 'Reading your description with online AI. Only the text is sent.' : 'Reading your description on this computer, offline.'}</p>
          <button class="btn btn-ghost sm" data-act="cancel">Cancel</button>
        </div>`;
      return;
    }
    if (s.songStatus === 'error') {
      root.innerHTML = `
        <div class="song-state song-error" role="alert">
          <h2>Couldn't build this song</h2>
          <p>${esc(s.songError ?? 'Try again, or remove a few moods.')}</p>
          <div class="row"><button class="btn btn-primary" data-act="retry">Try again</button></div>
        </div>`;
      return;
    }
    const song = s.song;
    const sec = song?.sections[s.selectedSection];
    if (!song || !sec) {
      root.innerHTML = `
        <div class="song-state song-empty">
          <h2>Build a song from your idea</h2>
          <p>Describe the song you hear, pick a few moods and press Build song. Every section is written around the chords you played.</p>
          <ol class="steps">
            <li><b>Describe it</b><span>A few words and moods. Works offline.</span></li>
            <li><b>Pick per section</b><span>Three options for every part of the song.</span></li>
            <li><b>Drag to your DAW</b><span>One section or the whole song as MIDI.</span></li>
          </ol>
        </div>`;
      return;
    }

    const focus = s.focusedOption ?? sec.chosen;
    const n = song.sections.length;
    root.innerHTML = `
      <div class="grid-head sec-head">
        <div class="sec-nav" role="group" aria-label="Section">
          <button class="icon-btn sm" data-act="prev" aria-label="Previous section" ${s.selectedSection === 0 ? 'disabled' : ''}>${icon.chevron()}</button>
          <button class="icon-btn sm" data-act="next" aria-label="Next section" ${s.selectedSection >= n - 1 ? 'disabled' : ''}>${icon.chevron()}</button>
        </div>
        <h2>${esc(sec.label)}</h2>
        <span class="grid-count">${sectionRange(song, s.selectedSection)}</span>
        ${sec.isIdea ? '<span class="idea-chip">Your idea</span>' : ''}
        <span class="grid-hint">Enter to use, Space to listen</span>
        <div class="scope" role="group" aria-label="What to drag">
          <span class="scope-label">Drag</span>
          <div class="seg-ctl sm" role="radiogroup" aria-label="Drag scope">
            <button role="radio" aria-checked="${s.dragScope === 'section'}" data-act="scope-section">Section</button>
            <button role="radio" aria-checked="${s.dragScope === 'song'}" data-act="scope-song">Whole song</button>
          </div>
        </div>
      </div>
      <div class="grid opt-grid" role="listbox" aria-label="Options for ${esc(sec.label)}">
        ${sec.options.map((o, i) => {
          const chosen = i === sec.chosen;
          const focused = i === focus;
          const playing = s.auditioning && s.auditionTarget === 'section';
          const st = strip(s, i);
          return `<article class="card opt-card${focused ? ' is-selected' : ''}${chosen ? ' is-chosen' : ''}${playing && focused ? ' is-playing' : ''}"
                role="option" aria-selected="${focused}" tabindex="${focused ? 0 : -1}" data-opt="${i}"
                aria-label="${esc(o.title)}${chosen ? ', in use' : ''}">
              <header class="card-head">
                <h3 title="${esc(o.title)}">${esc(o.title)}</h3>
                ${chosen ? `<span class="in-use">${icon.check()}In use</span>` : ''}
              </header>
              <div class="mini-wrap"><span class="mini-bars num" aria-hidden="true" title="Starts at bar ${sec.startBar + 1}">${sec.startBar + 1}</span><div class="mini-strip" aria-hidden="true">${st.html}<span class="mini-playhead"></span></div></div>
              <p class="card-summary">${esc(o.explanation)}</p>
              <footer class="card-foot">
                <span class="opt-meters">
                  ${meter('Energy', o.energy, 'm-energy', 'var(--so-accent)')}
                  ${meter('Tension', o.tension, 'm-tension', tensionColor(o.tension / 100))}
                </span>
                ${chosen ? '' : `<button class="btn btn-secondary sm" data-act="use">Use this</button>`}
                <button class="play-btn" data-act="play" aria-label="${playing && focused ? 'Stop' : 'Listen to'} ${esc(o.title)}">${playing && focused ? icon.pause(12) : icon.play(12)}</button>
              </footer>
            </article>`;
        }).join('')}
      </div>`;
  }

  store.subscribe((_, changed) => {
    if (['mode', 'song', 'songStatus', 'songProgress', 'selectedSection', 'focusedOption', 'dragScope', 'auditioning', 'auditionTarget', 'settings', 'aiStatus', 'songError'].some((k) => changed.has(k as never))) render();
  });
  render();
  return root;
}
