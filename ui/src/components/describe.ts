// Build out: DescriptionPanel (takes the ControlRail's place). Textarea, mood chips, idea and
// structure selectors, the opt-in online AI toggle and Build song / Shuffle.
import { IDEA_SECTIONS, MOODS, STRUCTURES } from '../bridge/contract';
import type { BuildForm, Store } from '../state/store';
import { IDEA_LABEL, MOOD_LABEL, STRUCTURE_LABEL } from '../state/song';
import type { IdeaSection, Mood, Structure } from '../state/types';
import { el, esc, icon } from '../ui/dom';

export interface DescribeActions {
  update(patch: Partial<BuildForm>): void;
  setOnlineAi(on: boolean): void;
  build(): void;
  shuffle(): void;
  openAiSettings(): void;
}

const MOOD_HINT: Partial<Record<Mood, string>> = {
  dark: 'Minor keys, low voicings, fewer bright chords',
  hopeful: 'Lifts toward major chords in the big sections',
  bittersweet: 'Major chords with a borrowed minor sigh',
  gritty: 'Raw, repetitive, half-step tension',
  cinematic: 'Long builds and wide swells',
  soulful: 'Gospel-style passing chords and 9ths',
};

export function DescriptionPanel(store: Store, actions: DescribeActions) {
  const root = el('section', { class: 'describe', 'aria-label': 'Describe the song' });
  root.innerHTML = `
    <div class="rail-group d-text">
      <label class="rail-label" for="song-desc">Describe it</label>
      <textarea id="song-desc" rows="3" maxlength="280" spellcheck="false"
        placeholder="Describe the song you hear, e.g. dark late-night R&amp;B, hopeful chorus"></textarea>
    </div>
    <div class="rail-sep" aria-hidden="true"></div>
    <div class="rail-group d-moods">
      <div class="rail-label" id="mood-label">Mood <span class="d-count num" data-slot="count"></span></div>
      <div class="mood-chips" role="group" aria-labelledby="mood-label">
        ${MOODS.map((m) => `<button class="chip" data-mood="${m}" aria-pressed="false" title="${esc(MOOD_HINT[m] ?? MOOD_LABEL[m])}"><i></i>${MOOD_LABEL[m]}</button>`).join('')}
      </div>
    </div>
    <div class="rail-sep" aria-hidden="true"></div>
    <div class="rail-group d-opts">
      <div class="rail-label" id="idea-label">My idea is the</div>
      <div class="seg-ctl" role="radiogroup" aria-labelledby="idea-label" data-slot="idea">
        ${IDEA_SECTIONS.map((k) => `<button role="radio" data-idea="${k}" aria-checked="false">${IDEA_LABEL[k]}</button>`).join('')}
      </div>
      <div class="rail-row d-structure">
        <label class="rail-label inline" for="song-structure">Structure</label>
        <span class="mini-select"><select id="song-structure">${STRUCTURES.map((k) => `<option value="${k}">${esc(STRUCTURE_LABEL[k])}</option>`).join('')}</select></span>
      </div>
      <div class="d-ai" data-slot="ai"></div>
    </div>
    <div class="rail-group g-go">
      <button class="btn btn-primary" data-act="build">${icon.song()}<span>Build song</span></button>
      <button class="btn btn-secondary" data-act="shuffle">${icon.shuffle()}<span>Shuffle</span></button>
    </div>`;

  const text = root.querySelector<HTMLTextAreaElement>('textarea')!;
  const structure = root.querySelector<HTMLSelectElement>('#song-structure')!;
  const idea = root.querySelector<HTMLElement>('[data-slot=idea]')!;
  const ai = root.querySelector<HTMLElement>('[data-slot=ai]')!;
  const count = root.querySelector<HTMLElement>('[data-slot=count]')!;
  const build = root.querySelector<HTMLButtonElement>('[data-act=build]')!;
  const shuffle = root.querySelector<HTMLButtonElement>('[data-act=shuffle]')!;

  text.addEventListener('input', () => actions.update({ description: text.value }));
  text.addEventListener('keydown', (e) => {
    // Enter builds; Shift+Enter adds a line (the compact layout shows one line only).
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); actions.build(); }
  });
  structure.addEventListener('change', () => actions.update({ structure: structure.value as Structure }));

  root.addEventListener('click', (e) => {
    const t = e.target as HTMLElement;
    const mood = t.closest<HTMLElement>('[data-mood]')?.dataset.mood as Mood | undefined;
    if (mood) {
      const cur = store.get().build.moods;
      actions.update({ moods: cur.includes(mood) ? cur.filter((m) => m !== mood) : [...cur, mood] });
    }
    const ideaK = t.closest<HTMLElement>('[data-idea]')?.dataset.idea as IdeaSection | undefined;
    if (ideaK) actions.update({ ideaSection: ideaK });
    const act = t.closest<HTMLElement>('[data-act]')?.dataset.act;
    if (act === 'build') actions.build();
    if (act === 'shuffle') actions.shuffle();
    if (act === 'ai-toggle') actions.setOnlineAi(!store.get().settings.useOnlineAi);
    if (act === 'ai-settings') actions.openAiSettings();
  });

  // Radiogroup arrows for the idea selector.
  idea.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    e.stopPropagation();
    const cur = IDEA_SECTIONS.indexOf(store.get().build.ideaSection);
    const next = IDEA_SECTIONS[(cur + (e.key === 'ArrowRight' ? 1 : IDEA_SECTIONS.length - 1)) % IDEA_SECTIONS.length];
    actions.update({ ideaSection: next });
    requestAnimationFrame(() => idea.querySelector<HTMLElement>(`[data-idea=${next}]`)?.focus());
  });

  function render() {
    const s = store.get();
    const f = s.build;
    if (document.activeElement !== text && text.value !== f.description) text.value = f.description;
    structure.value = f.structure;
    root.querySelectorAll<HTMLElement>('[data-mood]').forEach((b) => b.setAttribute('aria-pressed', String(f.moods.includes(b.dataset.mood as Mood))));
    count.textContent = f.moods.length ? `${f.moods.length} picked` : '';
    idea.querySelectorAll<HTMLButtonElement>('[data-idea]').forEach((b) => {
      const on = b.dataset.idea === f.ideaSection;
      b.setAttribute('aria-checked', String(on));
      b.tabIndex = on ? 0 : -1;
    });

    const hasKey = s.aiStatus.hasKey;
    const on = hasKey && s.settings.useOnlineAi;
    ai.innerHTML = `
      <button class="switch" role="switch" aria-checked="${on}" data-act="ai-toggle" aria-describedby="ai-note"
        title="${hasKey ? 'Sends your description text only, never audio' : 'Add an API key in Settings to turn this on'}" ${hasKey ? '' : 'disabled'}>
        <span class="switch-track"><i></i></span><span>Use online AI</span>
      </button>
      ${hasKey
        ? `<small class="d-note" id="ai-note">Sends your description text only, never audio</small>`
        : `<small class="d-note" id="ai-note">Add a key in <button class="link" data-act="ai-settings">Settings</button> to turn this on</small>`}`;

    const busy = s.songStatus === 'building';
    build.disabled = busy || !s.analysis;
    build.classList.toggle('is-busy', busy);
    build.querySelector('span')!.textContent = busy ? 'Writing…' : s.song ? 'Build again' : 'Build song';
    shuffle.disabled = busy || !s.song;
    shuffle.title = s.song ? 'Same description, new options' : 'Build a song first';
  }

  store.subscribe((_, changed) => {
    if (['build', 'settings', 'aiStatus', 'songStatus', 'song', 'analysis'].some((k) => changed.has(k as never))) render();
  });
  render();
  return root;
}
