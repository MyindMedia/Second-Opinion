import { callNative, params } from '../bridge/juce';
import type { Settings, Store } from '../state/store';
import { chordHtml, formatChord, isParsed, parseChord, wrap } from '../theory/chords';
import { el, esc, icon } from '../ui/dom';

// ---------------------------------------------------------------------------------------------
// Stage: empty, capturing, analyzing, error (fills the body region when not "ready")
// ---------------------------------------------------------------------------------------------
export interface StageActions {
  capture(): void;
  stopCapture(): void;
  cancel(): void;
  retry(): void;
  loadDemo(): void;
  setTempo(bpm: number): void;
  openFile(): void;
}

export function Stage(store: Store, actions: StageActions) {
  const root = el('div', { class: 'stage', 'aria-live': 'polite' });

  root.addEventListener('click', (e) => {
    const act = (e.target as HTMLElement).closest<HTMLElement>('[data-act]')?.dataset.act;
    if (act === 'capture') actions.capture();
    if (act === 'stop') actions.stopCapture();
    if (act === 'cancel') actions.cancel();
    if (act === 'retry') actions.retry();
    if (act === 'demo') actions.loadDemo();
    if (act === 'open') actions.openFile();
    if (act === 'tempo') {
      const v = Number(root.querySelector<HTMLInputElement>('input[name=bpm]')?.value);
      if (v >= 40 && v <= 240) actions.setTempo(v);
    }
  });

  // Drops are handled window-wide in main.ts (decode in JS, then analyzePcm); the drop zone only highlights.

  let timer = 0;

  function render() {
    const s = store.get();
    root.hidden = s.phase === 'ready';
    root.dataset.phase = s.phase;
    window.clearInterval(timer);

    if (s.phase === 'empty') {
      root.innerHTML = `
        <div class="empty">
          <div class="drop-zone">
            <svg class="staff" viewBox="0 0 400 60" preserveAspectRatio="none" aria-hidden="true">
              ${[0, 1, 2, 3, 4].map((i) => `<line x1="0" x2="400" y1="${10 + i * 10}" y2="${10 + i * 10}"/>`).join('')}
            </svg>
            <div class="drop-icon">${icon.drop()}</div>
            <h1>Drop a vocal, loop or bounce</h1>
            <p>WAV, AIFF, FLAC or MP3, up to 3 minutes. Nothing leaves your computer.</p>
            <div class="empty-actions">
              <button class="btn btn-primary lg" data-act="capture"><span class="rec-dot">${icon.record(10)}</span>Capture from this track</button>
              <button class="btn btn-secondary lg" data-act="open">${icon.folder()}Open audio file</button>
            </div>
            <button class="link demo-link" data-act="demo">Or open the example session</button>
          </div>
          <ol class="steps">
            <li><b>Capture</b><span>Arm Capture and press play, or drop a file.</span></li>
            <li><b>Reharm or build out</b><span>Audition new chords, or grow the idea into a full song.</span></li>
            <li><b>Drag to your DAW</b><span>Chords, bass and top line land as MIDI.</span></li>
          </ol>
        </div>`;
    } else if (s.phase === 'armed' || s.phase === 'recording') {
      const armed = s.phase === 'armed';
      root.innerHTML = `
        <div class="capture-panel">
          <div class="rec-ring${armed ? ' is-armed' : ' is-rec'}">${icon.record(18)}</div>
          <h1>${armed ? 'Press play in your DAW' : 'Listening'}</h1>
          <p>${armed ? 'Capture starts with the transport and stops when you stop playback.' : 'Recording in sync with your session. Audio passes through untouched.'}</p>
          <div class="bar-counter num" aria-label="Bar ${s.recordingBar} of ${s.captureMaxBars}" style="grid-template-columns:repeat(${s.captureMaxBars}, 1fr)">
            ${Array.from({ length: s.captureMaxBars }, (_, i) => `<i class="${i < s.recordingBar ? 'on' : ''}"></i>`).join('')}
          </div>
          <div class="capture-meta num"><span>${armed ? 'Waiting for transport' : `Bar ${s.recordingBar} of ${s.captureMaxBars}`}</span><span class="meter big" style="--lvl:${armed ? 0 : s.inputLevel}"></span></div>
          <button class="btn btn-secondary" data-act="stop">${icon.stop()}<span>${armed ? 'Disarm' : 'Stop and analyze'}</span></button>
        </div>`;
    } else if (s.phase === 'analyzing') {
      const elapsed = () => ((performance.now() - s.progressStarted) / 1000).toFixed(1);
      root.innerHTML = `
        <div class="analyze-panel" role="progressbar" aria-label="Analyzing" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${Math.round(s.progress.reduce((a, p) => a + p.pct, 0) / s.progress.length)}">
          <h1>${s.progress[0]?.label === 'Decoding file' && s.progress[0].pct < 100 ? 'Decoding your file' : 'Reading your audio'}</h1>
          <ol class="stages">
            ${s.progress.map((p) => `
              <li class="${p.pct >= 100 ? 'done' : p.pct > 0 ? 'active' : ''}">
                <span class="stage-name">${esc(p.label)}</span>
                <span class="stage-bar"><i style="width:${p.pct}%"></i></span>
                <span class="stage-pct num">${p.pct >= 100 ? 'Done' : p.pct > 0 ? `${Math.round(p.pct)}%` : ''}</span>
              </li>`).join('')}
          </ol>
          <div class="analyze-foot">
            <span class="num elapsed">${elapsed()} s</span>
            <span class="muted">Runs on this computer, offline</span>
            <button class="btn btn-ghost" data-act="cancel">Cancel</button>
          </div>
        </div>`;
      timer = window.setInterval(() => {
        const e = root.querySelector('.elapsed');
        if (e) e.textContent = `${elapsed()} s`;
      }, 100);
    } else if (s.phase === 'error') {
      root.innerHTML = s.needsTempo
        ? `<div class="error-panel">
            <h1>No tempo from the host</h1>
            <p>Set the BPM so bar lines match your session. The downbeat is taken from the start of the audio.</p>
            <div class="tempo-row"><label>BPM <input class="num" name="bpm" type="number" min="40" max="240" value="120"/></label>
            <button class="btn btn-primary" data-act="tempo">Set tempo and analyze</button></div>
          </div>`
        : `<div class="error-panel">
            <h1>${esc(s.errorTitle ?? "Couldn't find a steady pitch")}</h1>
            <p>${esc(s.errorMessage ?? 'Try soloing the vocal, or capture a cleaner section.')}</p>
            <div class="tempo-row"><button class="btn btn-primary" data-act="retry">Retry</button><button class="btn btn-ghost" data-act="open">Open audio file</button><button class="btn btn-ghost" data-act="demo">Open the example session</button></div>
          </div>`;
    }
  }

  store.subscribe((_, changed) => {
    if (['phase', 'progress', 'recordingBar', 'inputLevel', 'needsTempo', 'captureMaxBars', 'errorMessage', 'errorTitle'].some((k) => changed.has(k as never))) render();
  });
  render();
  return root;
}

// ---------------------------------------------------------------------------------------------
// ChordPopover (FR-AN-03): top 3 alternatives, typed correction, lock, split/merge
// ---------------------------------------------------------------------------------------------
export interface PopoverActions {
  replace(segmentId: string, symbol: string): void;
  toggleLock(segmentId: string): void;
  split(segmentId: string): void;
  merge(segmentId: string): void;
  close(): void;
}

function miniKeys(pcs: number[], bassPc: number) {
  const whites = [0, 2, 4, 5, 7, 9, 11];
  const blacks = [1, 3, 6, 8, 10];
  const bx: Record<number, number> = { 1: 1, 3: 2, 6: 4, 8: 5, 10: 6 };
  return `<svg class="mini-keys" viewBox="0 0 70 22" width="70" height="22" aria-hidden="true">
    ${whites.map((pc, i) => `<rect x="${i * 10}" y="0" width="9.4" height="22" rx="1.5" class="wk${pcs.includes(pc) ? ' on' : ''}${pc === bassPc ? ' bass' : ''}"/>`).join('')}
    ${blacks.map((pc) => `<rect x="${bx[pc] * 10 - 3.2}" y="0" width="6.4" height="13" rx="1" class="bk${pcs.includes(pc) ? ' on' : ''}${pc === bassPc ? ' bass' : ''}"/>`).join('')}
  </svg>`;
}

export function ChordPopover(store: Store, actions: PopoverActions) {
  const root = el('div', { class: 'popover', role: 'dialog', 'aria-label': 'Confirm or correct this chord', hidden: true });
  let anchor: HTMLElement | null = null;

  root.addEventListener('click', (e) => {
    const t = (e.target as HTMLElement).closest<HTMLElement>('[data-act]');
    const id = store.get().popoverSegmentId;
    if (!t || !id) return;
    const act = t.dataset.act;
    if (act === 'alt') actions.replace(id, t.dataset.symbol!);
    if (act === 'lock') actions.toggleLock(id);
    if (act === 'split') actions.split(id);
    if (act === 'merge') actions.merge(id);
    if (act === 'close') actions.close();
  });

  root.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') { e.stopPropagation(); actions.close(); anchor?.focus(); }
    e.stopPropagation(); // typing must not trigger global shortcuts
  });

  root.addEventListener('submit', async (e) => {
    e.preventDefault();
    const input = root.querySelector<HTMLInputElement>('input[name=chord]')!;
    const msg = root.querySelector<HTMLElement>('.field-msg')!;
    const id = store.get().popoverSegmentId!;
    const native = await callNative<{ ok: boolean; symbol?: string; error?: string }>('parseChord', input.value);
    const result = native ?? (() => {
      const p = parseChord(input.value);
      return isParsed(p) ? { ok: true, symbol: formatChord(p, 'pop') } : { ok: false, error: p.error };
    })();
    if (result.ok && result.symbol) actions.replace(id, result.symbol);
    else {
      msg.textContent = result.error ?? 'Not a chord we know yet';
      input.setAttribute('aria-invalid', 'true');
    }
  });

  root.addEventListener('input', (e) => {
    const input = e.target as HTMLInputElement;
    const msg = root.querySelector<HTMLElement>('.field-msg')!;
    if (!input.value.trim()) { msg.textContent = ''; input.removeAttribute('aria-invalid'); return; }
    const p = parseChord(input.value);
    msg.innerHTML = isParsed(p) ? `Notes ${p.pcs.map((pc) => ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'G♭', 'G', 'A♭', 'A', 'B♭', 'B'][wrap(pc)]).join(' ')}${p.bass ? `, bass ${esc(p.bass)}` : ''}` : esc(p.error);
    input.toggleAttribute('aria-invalid', !isParsed(p));
  });

  function position() {
    if (!anchor || root.hidden) return;
    const r = anchor.getBoundingClientRect();
    const pw = root.offsetWidth, ph = root.offsetHeight;
    let left = r.left + r.width / 2 - pw / 2;
    left = Math.max(8, Math.min(window.innerWidth - pw - 8, left));
    let top = r.bottom + 8;
    if (top + ph > window.innerHeight - 56) top = Math.max(8, r.top - ph - 8);
    root.style.left = `${left}px`;
    root.style.top = `${top}px`;
  }

  function render() {
    const s = store.get();
    const seg = s.analysis?.segments.find((x) => x.id === s.popoverSegmentId);
    root.hidden = !seg;
    if (!seg) return;
    anchor = document.querySelector<HTMLElement>(`[data-seg="${seg.id}"]`);
    const locked = s.locked.has(seg.id);
    const readings = [{ symbol: seg.symbol, confidence: seg.confidence }, ...seg.alternatives].slice(0, 4);
    root.innerHTML = `
      <div class="pop-head">
        <span class="pop-title">Bar ${Math.floor(seg.startBeat / 4) + 1}${seg.startBeat % 4 ? `, beat ${(seg.startBeat % 4) + 1}` : ''}</span>
        ${seg.confidence < 0.5 ? '<span class="pop-unsure">Unsure</span>' : ''}
        <button class="icon-btn sm" data-act="close" aria-label="Close">${icon.close()}</button>
      </div>
      <ul class="pop-alts" role="list">
        ${readings.map((r, i) => {
          const p = parseChord(r.symbol);
          const keys = isParsed(p) ? miniKeys(p.pcs, p.bassPc) : '';
          return `<li><button class="alt${i === 0 ? ' is-current' : ''}" data-act="alt" data-symbol="${esc(r.symbol)}">
            <span class="alt-sym">${chordHtml(r.symbol, s.settings.notation)}</span>
            ${keys}
            <span class="alt-conf"><span class="conf-bar"><i style="width:${Math.round(r.confidence * 100)}%"></i></span><span class="num">${Math.round(r.confidence * 100)}%</span></span>
            ${i === 0 ? '<span class="alt-tag">Heard</span>' : ''}
          </button></li>`;
        }).join('')}
      </ul>
      <form class="pop-type" autocomplete="off">
        <label for="chord-input">Type a chord</label>
        <div class="field-row"><input id="chord-input" name="chord" placeholder="e.g. G/B, Am7, F♯m7♭5" spellcheck="false"/><button class="btn btn-primary sm" type="submit">Use</button></div>
        <span class="field-msg" aria-live="polite"></span>
      </form>
      <div class="pop-actions">
        <button class="btn btn-ghost sm${locked ? ' is-on' : ''}" data-act="lock" aria-pressed="${locked}">${icon.lock()}${locked ? 'Locked' : 'Lock'}<kbd>L</kbd></button>
        <span class="spacer"></span>
        <button class="btn btn-ghost sm" data-act="split" ${seg.beats < 2 ? 'disabled' : ''}>Split here</button>
        <button class="btn btn-ghost sm" data-act="merge">Merge with next</button>
      </div>`;
    requestAnimationFrame(position);
  }

  window.addEventListener('resize', position);
  document.addEventListener('scroll', position, true);
  store.subscribe((_, changed) => {
    if (changed.has('popoverSegmentId') || changed.has('locked') || changed.has('analysis') || changed.has('settings')) render();
  });
  return root;
}

// ---------------------------------------------------------------------------------------------
// Settings sheet (FR-SET-01..05)
// ---------------------------------------------------------------------------------------------
export interface AiKeyActions {
  save(key: string): void;
  remove(): void;
}

export function SettingsSheet(store: Store, update: (patch: Partial<Settings>) => void, close: () => void, clearCache: () => void, ai: AiKeyActions) {
  const root = el('div', { class: 'sheet-backdrop', hidden: true });
  const TABS = ['Notation', 'Sound', 'MIDI export', 'Analysis', 'AI', 'Interface', 'About'];

  root.addEventListener('click', (e) => {
    if (e.target === root) return close();
    const t = (e.target as HTMLElement).closest<HTMLElement>('[data-tab],[data-set],[data-act]');
    if (!t) return;
    if (t.dataset.tab) store.set({ settingsTab: t.dataset.tab });
    if (t.dataset.act === 'close') close();
    if (t.dataset.act === 'clear') clearCache();
    if (t.dataset.act === 'ai-remove') ai.remove();
    if (t.dataset.act === 'ai-replace') { replacing = true; render(); root.querySelector<HTMLInputElement>('input[name=aikey]')?.focus(); }
    if (t.dataset.set === 'useOnlineAi=toggle' && !store.get().aiStatus.hasKey) return;
    if (t.dataset.set) {
      const [k, v] = t.dataset.set.split('=');
      const cur = store.get().settings as unknown as Record<string, unknown>;
      const value = v === 'toggle' ? !cur[k] : /^\d+$/.test(v) ? Number(v) : v;
      update({ [k]: value } as Partial<Settings>);
    }
  });
  root.addEventListener('input', (e) => {
    const t = e.target as HTMLInputElement;
    if (t.name === 'scale') update({ scale: Number(t.value) });
    if (t.name === 'level') params.previewLevel.set(Number(t.value));
  });
  root.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } });
  root.addEventListener('submit', (e) => {
    e.preventDefault();
    const input = root.querySelector<HTMLInputElement>('input[name=aikey]');
    if (!input) return;
    const key = input.value;
    input.value = '';      // the key never lingers in the DOM; it lives only in the plugin's store
    replacing = false;
    if (key.trim()) ai.save(key);
  });
  let replacing = false;

  const radio = (key: keyof Settings, value: string | number, label: string, s: Settings) =>
    `<button role="radio" aria-checked="${s[key] === value}" data-set="${key}=${value}">${label}</button>`;
  const toggle = (key: keyof Settings, label: string, s: Settings, hint = '') =>
    `<button class="switch" role="switch" aria-checked="${!!s[key]}" data-set="${key}=toggle"><span class="switch-track"><i></i></span><span>${label}${hint ? `<small>${hint}</small>` : ''}</span></button>`;
  const row = (label: string, control: string, hint = '') => `<div class="set-row"><div class="set-label">${label}${hint ? `<small>${hint}</small>` : ''}</div><div class="set-control">${control}</div></div>`;

  function body(tab: string, s: Settings) {
    switch (tab) {
      case 'Notation':
        return row('Chord style', `<div class="seg-ctl" role="radiogroup">${radio('notation', 'pop', 'Pop <span class="ex">Cmaj7 · C/E</span>', s)}${radio('notation', 'jazz', 'Jazz <span class="ex">CΔ7 · C-7</span>', s)}</div>`)
          + row('Slash chords', toggle('preferSlash', 'Prefer slash chords', s), 'G/F stays G/F instead of a rarer name')
          + row('Accidentals', `<div class="seg-ctl" role="radiogroup">${radio('spelling', 'auto', 'Auto', s)}${radio('spelling', 'flats', '♭ Flats', s)}${radio('spelling', 'sharps', '♯ Sharps', s)}</div>`, 'Auto follows the key');
      case 'Sound':
        return row('Preview sound', `<div class="seg-ctl" role="radiogroup">${radio('previewSound', 'keys', 'Keys', s)}${radio('previewSound', 'pad', 'Pad', s)}${radio('previewSound', 'off', 'Off', s)}</div>`)
          + row('Preview level', `<input type="range" name="level" min="-60" max="0" step="0.5" value="${params.previewLevel.value}"/><span class="num">${params.previewLevel.value.toFixed(1)} dB</span>`, 'Mixed in only while you audition');
      case 'MIDI export':
        return row('Resolution', `<div class="seg-ctl" role="radiogroup">${radio('ppq', 480, '480 PPQ', s)}${radio('ppq', 960, '960 PPQ', s)}</div>`)
          + row('Velocity', `<div class="seg-ctl" role="radiogroup">${radio('velocity', 'flat', 'Flat', s)}${radio('velocity', 'humanized', 'Humanized', s)}</div>`)
          + row('File names', `<code class="num">SecondOpinion_&lt;project&gt;_&lt;suggestion&gt;_&lt;lane&gt;.mid</code>`);
      case 'Analysis':
        return row('Quality', `<div class="seg-ctl" role="radiogroup">${radio('quality', 'fast', 'Fast', s)}${radio('quality', 'balanced', 'Balanced', s)}${radio('quality', 'accurate', 'Accurate', s)}</div>`, 'Accurate takes longer on long captures')
          + row('Source', `<div class="seg-ctl" role="radiogroup">${radio('sourceHint', 'auto', 'Auto', s)}${radio('sourceHint', 'vocal', 'Vocal', s)}${radio('sourceHint', 'keys', 'Keys', s)}${radio('sourceHint', 'guitar', 'Guitar', s)}${radio('sourceHint', 'loop', 'Loop', s)}</div>`, 'Vocal reads harmony from the melody')
          + row('Cache', `<button class="btn btn-secondary sm" data-act="clear">Clear captured audio</button>`, 'Captured audio is kept on this computer only');
      case 'AI': {
        const st = store.get().aiStatus;
        const keyForm = `<form class="field-row ai-key" autocomplete="off">
            <input name="aikey" type="password" autocomplete="off" spellcheck="false" placeholder="Paste your API key (sk-...)" aria-label="API key"/>
            <button class="btn btn-primary sm" type="submit">Save key</button>
          </form>`;
        const keyControl = st.hasKey && !replacing
          ? `<code class="num">${esc(st.maskedKey)}</code><button class="btn btn-ghost sm" data-act="ai-replace">Replace</button><button class="btn btn-ghost sm" data-act="ai-remove">Remove</button>`
          : keyForm;
        return row('Online AI', `<button class="switch" role="switch" aria-checked="${st.hasKey && s.useOnlineAi}" data-set="useOnlineAi=toggle" ${st.hasKey ? '' : 'disabled'}><span class="switch-track"><i></i></span><span>Read descriptions with online AI${st.hasKey ? '' : '<small>Add a key below to turn this on</small>'}</span></button>`, 'Works offline when this is off')
          + row('API key', `${keyControl}${st.lastError ? `<span class="field-msg is-error" role="alert">${esc(st.lastError)}</span>` : ''}`, 'Stored by the plugin in your user folder, never in the project')
          + `<div class="ai-privacy">
              <h4>What is sent</h4>
              <p>Only the words you type in the Build out description, and only when this is on. Your audio, chords and MIDI never leave this computer.</p>
              <p>If the request fails or there is no network, the description is read offline instead, so Build out always works.</p>
            </div>`;
      }
      case 'Interface':
        return row('UI scale', `<input type="range" name="scale" min="75" max="200" step="5" value="${s.scale}"/><span class="num">${s.scale}%</span>`)
          + row('Motion', toggle('reduceMotion', 'Reduce motion', s));
      default:
        return `<div class="about">
            <div class="about-brand"><span class="wordmark">Second Opinion</span><span class="num muted">Version 0.1.0 (Phase 1 preview)</span></div>
            <p>Works fully offline. Your audio never leaves this computer, and there is no account or telemetry. Online AI for Build out is opt-in and sends description text only.</p>
            <h4>Third-party software</h4>
            <ul class="licenses">
              <li><b>JUCE 9</b><span>JUCE licence (Starter)</span></li>
              <li><b>VST 3 SDK</b><span>MIT</span></li>
              <li><b>IBM Plex Sans, Mono, Sans Condensed</b><span>SIL Open Font License 1.1</span></li>
              <li><b>Noto Music (accidental glyphs)</b><span>SIL Open Font License 1.1</span></li>
              <li><b>Basic Pitch</b><span>Apache-2.0 (coming in a later build)</span></li>
              <li><b>ONNX Runtime</b><span>MIT (coming in a later build)</span></li>
            </ul>
          </div>`;
    }
  }

  function render() {
    const s = store.get();
    root.hidden = !s.settingsOpen;
    if (!s.settingsOpen) return;
    const tab = s.settingsTab;
    root.innerHTML = `
      <div class="sheet" role="dialog" aria-modal="true" aria-label="Settings">
        <nav class="sheet-tabs" role="tablist" aria-label="Settings sections">
          <h2>Settings</h2>
          ${TABS.map((t) => `<button role="tab" aria-selected="${t === tab}" data-tab="${t}">${t}</button>`).join('')}
        </nav>
        <div class="sheet-body" role="tabpanel">
          <div class="sheet-head"><h3>${tab}</h3><button class="icon-btn" data-act="close" aria-label="Close settings">${icon.close()}</button></div>
          ${body(tab, s.settings)}
        </div>
      </div>`;
    root.querySelector<HTMLElement>('[aria-selected=true]')?.focus({ focusVisible: false } as FocusOptions);
  }

  store.subscribe((_, changed) => {
    if (changed.has('settingsOpen') || changed.has('settingsTab')) replacing = false;
    if (changed.has('settingsOpen') || changed.has('settingsTab') || changed.has('settings') || changed.has('aiStatus')) render();
  });
  return root;
}

// ---------------------------------------------------------------------------------------------
// Banner + toast
// ---------------------------------------------------------------------------------------------
export function Banner(store: Store, onReview: () => void) {
  const root = el('div', { class: 'banner', role: 'status', hidden: true });
  root.addEventListener('click', (e) => {
    const act = (e.target as HTMLElement).closest<HTMLElement>('[data-act]')?.dataset.act;
    if (act === 'review') onReview();
    if (act === 'dismiss') store.set({ bannerDismissed: true });
  });
  const render = () => {
    const s = store.get();
    const low = s.analysis?.segments.filter((x) => x.confidence < 0.5 && !x.userCorrected).length ?? 0;
    root.hidden = s.phase !== 'ready' || s.mode === 'build' || s.bannerDismissed || low === 0;
    root.innerHTML = `<span class="banner-mark" aria-hidden="true">?</span>
      <span>${low} chord${low === 1 ? ' is' : 's are'} uncertain. Click ${low === 1 ? 'it' : 'one'} to confirm.</span>
      <button class="link" data-act="review">Review first</button>
      <button class="icon-btn sm" data-act="dismiss" aria-label="Dismiss">${icon.close()}</button>`;
  };
  store.subscribe((_, c) => { if (c.has('analysis') || c.has('bannerDismissed') || c.has('phase') || c.has('mode')) render(); });
  render();
  return root;
}

export function Toast(store: Store, onAction: () => void) {
  const root = el('div', { class: 'toast', role: 'alert', hidden: true });
  let t = 0;
  root.addEventListener('click', (e) => {
    const act = (e.target as HTMLElement).closest<HTMLElement>('[data-act]')?.dataset.act;
    if (act === 'action') onAction();
    if (act) store.set({ toast: undefined });
  });
  store.subscribe((s, c) => {
    if (!c.has('toast')) return;
    root.hidden = !s.toast;
    if (!s.toast) return;
    root.className = `toast toast-${s.toast.kind}`;
    root.innerHTML = `<span>${esc(s.toast.message)}</span>${s.toast.action ? `<button class="link" data-act="action">${esc(s.toast.action)}</button>` : ''}<button class="icon-btn sm" data-act="close" aria-label="Dismiss">${icon.close()}</button>`;
    window.clearTimeout(t);
    if (s.toast.kind === 'info') t = window.setTimeout(() => store.set({ toast: undefined }), 3200);
  });
  return root;
}
