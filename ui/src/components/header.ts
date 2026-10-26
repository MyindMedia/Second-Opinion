import type { Mode, Store } from '../state/store';
import { el, esc, icon } from '../ui/dom';

export interface HeaderActions {
  capture(): void;
  stopCapture(): void;
  openFile(): void;
  setMode(mode: Mode): void;
  setKey(key: string): void;
  openSettings(): void;
  toggleDrawer(): void;
  undo(): void;
  redo(): void;
}

const KEYS = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'].flatMap((k) => [`${k} major`, `${k} minor`]);

// The mark: an original lane (teal) with a suggestion lane (brass) offset beneath it.
const MARK = `<svg class="mark" width="22" height="16" viewBox="0 0 22 16" aria-hidden="true">
  <rect x="1" y="2" width="13" height="5" rx="1.5" fill="var(--so-original)" opacity=".9"/>
  <rect x="7" y="9" width="14" height="5" rx="1.5" fill="var(--so-accent)"/>
</svg>`;

export function Header(store: Store, actions: HeaderActions) {
  const root = el('header', { class: 'header', role: 'banner' });

  root.innerHTML = `
    <div class="brand">${MARK}<span class="wordmark">Second Opinion</span></div>
    <div class="source" data-slot="source"></div>
    <div class="seg-ctl mode-switch" role="radiogroup" aria-label="Mode" data-slot="mode">
      <button role="radio" data-mode="reharm" aria-checked="true" title="Reharmonize the chords you played">Reharm</button>
      <button role="radio" data-mode="build" aria-checked="false" title="Build a full song from your idea">${icon.song()}Build out</button>
    </div>
    <div class="header-spacer"></div>
    <div class="header-status" data-slot="status"></div>
    <div class="header-actions">
      <button class="btn btn-ghost btn-open" data-act="open" title="Open audio file" aria-label="Open audio file">${icon.folder()}<span>Open</span></button>
      <button class="btn btn-capture" data-act="capture"></button>
      <span class="divider" aria-hidden="true"></span>
      <button class="icon-btn" data-act="undo" title="Undo (Cmd/Ctrl+Z)" aria-label="Undo">${icon.undo()}</button>
      <button class="icon-btn" data-act="redo" title="Redo (Shift+Cmd/Ctrl+Z)" aria-label="Redo">${icon.redo()}</button>
      <button class="icon-btn only-compact" data-act="drawer" title="Why this works" aria-label="Show explanation panel">${icon.panel()}</button>
      <button class="icon-btn" data-act="settings" title="Settings" aria-label="Settings">${icon.gear()}</button>
    </div>`;

  root.addEventListener('click', (e) => {
    const act = (e.target as HTMLElement).closest<HTMLElement>('[data-act]')?.dataset.act;
    const s = store.get();
    if (act === 'capture') (s.phase === 'armed' || s.phase === 'recording' ? actions.stopCapture : actions.capture)();
    if (act === 'open') actions.openFile();
    if (act === 'settings') actions.openSettings();
    const mode = (e.target as HTMLElement).closest<HTMLElement>('[data-mode]')?.dataset.mode as Mode | undefined;
    if (mode && mode !== s.mode && s.phase === 'ready') actions.setMode(mode);
    if (act === 'drawer') actions.toggleDrawer();
    if (act === 'undo') actions.undo();
    if (act === 'redo') actions.redo();
  });

  const source = root.querySelector<HTMLElement>('[data-slot=source]')!;
  const status = root.querySelector<HTMLElement>('[data-slot=status]')!;
  const capture = root.querySelector<HTMLButtonElement>('[data-act=capture]')!;
  const open = root.querySelector<HTMLButtonElement>('[data-act=open]')!;
  const modeSwitch = root.querySelector<HTMLElement>('[data-slot=mode]')!;

  // Arrow keys move between the two modes (radiogroup pattern).
  modeSwitch.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    e.stopPropagation();
    const s = store.get();
    if (s.phase !== 'ready') return;
    const next: Mode = s.mode === 'reharm' ? 'build' : 'reharm';
    actions.setMode(next);
    requestAnimationFrame(() => modeSwitch.querySelector<HTMLElement>(`[data-mode=${next}]`)?.focus());
  });

  source.addEventListener('change', (e) => {
    const t = e.target as HTMLSelectElement;
    if (t.name === 'key') actions.setKey(t.value);
  });

  function update() {
    const s = store.get();
    const a = s.analysis;
    const key = s.keyOverride ?? a?.key ?? '';

    if (a && (s.phase === 'ready' || s.phase === 'analyzing')) {
      source.innerHTML = `
        <span class="pill">
          <span class="pill-dot" aria-hidden="true"></span>
          <span>${a.source === 'captured' ? 'Captured' : esc(a.sourceName)} ${a.bars} bars</span>
          <span class="pill-sep" aria-hidden="true"></span>
          <span class="num">${Math.round(s.hostTempo ?? a.bpm)} BPM</span>
          <span class="pill-sep" aria-hidden="true"></span>
          <label class="key-select" title="Override the detected key">
            <span class="sr-only">Key</span>
            <select name="key">${KEYS.map((k) => `<option ${k === key ? 'selected' : ''}>${k}</option>`).join('')}</select>
            <span class="key-label">${esc(key)}${s.keyOverride ? '' : ` <span class="muted">detected</span>`}</span>
            ${icon.chevron()}
          </label>
        </span>`;
    } else {
      source.innerHTML = `<span class="pill pill-empty">No audio yet</span>`;
    }

    const capturing = s.phase === 'armed' || s.phase === 'recording';
    capture.classList.toggle('is-live', capturing);
    capture.classList.toggle('is-armed', s.phase === 'armed');
    capture.innerHTML = capturing ? `${icon.stop()}<span>Stop</span>` : `<span class="rec-dot">${icon.record(10)}</span><span>Capture</span>`;
    capture.disabled = s.phase === 'analyzing';
    open.disabled = s.phase === 'analyzing' || capturing;

    const ready = s.phase === 'ready';
    modeSwitch.classList.toggle('is-disabled', !ready);
    modeSwitch.title = ready ? '' : 'Capture or open audio first';
    modeSwitch.querySelectorAll<HTMLButtonElement>('[data-mode]').forEach((b) => {
      const on = b.dataset.mode === s.mode;
      b.setAttribute('aria-checked', String(on));
      b.tabIndex = on ? 0 : -1;
      b.disabled = !ready;
    });

    status.innerHTML =
      s.phase === 'armed' ? `<span class="status-armed">Press play in your DAW</span>`
      : s.phase === 'recording' ? `<span class="status-rec num">Bar ${s.recordingBar} of ${s.captureMaxBars}</span><span class="meter" style="--lvl:${s.inputLevel}"></span>`
      : s.phase === 'ready' && s.mode === 'reharm' ? `<span class="hint">${icon.drop()} Drop audio to replace</span>`
      : '';
  }

  store.subscribe((_, changed) => {
    if (['phase', 'analysis', 'keyOverride', 'recordingBar', 'inputLevel', 'hostTempo', 'mode', 'captureMaxBars'].some((k) => changed.has(k as never))) update();
  });
  update();
  return root;
}
