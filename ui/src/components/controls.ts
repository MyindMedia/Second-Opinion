// Parameter-bound controls. Each takes a bridge param (value + onChange + automated flag),
// so host automation and UI stay in sync (UI brief, Implementation note).
import { ChoiceParam, SliderParam, ToggleParam } from '../bridge/juce';
import { clamp, el, esc, tensionColor } from '../ui/dom';

const START = 135, SWEEP = 270;

function polar(cx: number, cy: number, r: number, deg: number) {
  const a = ((deg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

function arc(cx: number, cy: number, r: number, from: number, to: number) {
  const [x0, y0] = polar(cx, cy, r, from + 90);
  const [x1, y1] = polar(cx, cy, r, to + 90);
  const large = to - from > 180 ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

export interface KnobOptions {
  size: number;
  label: string;
  captions?: [string, string, string?];   // left, right, (middle)
  warm?: boolean;                          // tension: cool → warm arc
  format?: (v: number) => string;
  describe?: (v: number) => string;
}

let knobId = 0;

export function Knob(param: SliderParam, o: KnobOptions) {
  const id = `knob-${knobId++}`;
  const root = el('div', { class: `knob${o.warm ? ' knob-warm' : ''}`, style: `--size:${o.size}px` });
  const s = o.size, c = s / 2, r = s / 2 - 5;

  root.innerHTML = `
    <div class="knob-dial" role="slider" tabindex="0" aria-label="${esc(o.label)}" aria-valuemin="${param.start}" aria-valuemax="${param.end}">
      <svg width="${s}" height="${s}" viewBox="0 0 ${s} ${s}" aria-hidden="true">
        <defs>
          <linearGradient id="${id}-g" x1="0" y1="1" x2="1" y2="0">
            <stop offset="0" stop-color="var(--so-tension-0)"/>
            <stop offset="1" stop-color="var(--so-tension-100)"/>
          </linearGradient>
        </defs>
        <circle cx="${c}" cy="${c}" r="${r - 7}" class="knob-face"/>
        <path d="${arc(c, c, r, START, START + SWEEP)}" class="knob-track"/>
        <path class="knob-value" stroke="${o.warm ? `url(#${id}-g)` : 'var(--so-accent)'}"/>
        <line class="knob-pointer" x1="${c}" y1="${c - r + 10}" x2="${c}" y2="${c - r + 10 + s * 0.13}"/>
      </svg>
      <span class="knob-readout num"></span>
      <span class="badge-auto" title="Automated by the host" aria-hidden="true">A</span>
    </div>
    <div class="knob-label">${esc(o.label)}</div>
    ${o.captions ? `<div class="knob-captions"><span>${esc(o.captions[0])}</span>${o.captions[2] ? `<span>${esc(o.captions[2])}</span>` : ''}<span>${esc(o.captions[1])}</span></div>` : ''}`;

  const dial = root.querySelector<HTMLElement>('.knob-dial')!;
  const valuePath = root.querySelector<SVGPathElement>('.knob-value')!;
  const pointer = root.querySelector<SVGLineElement>('.knob-pointer')!;
  const readout = root.querySelector<HTMLElement>('.knob-readout')!;

  const norm = () => (param.value - param.start) / (param.end - param.start);

  function draw() {
    const n = clamp(norm(), 0, 1);
    const deg = START + SWEEP * n;
    valuePath.setAttribute('d', n > 0.002 ? arc(c, c, r, START, deg) : '');
    // The pointer is drawn pointing up (screen angle 270°); rotate it onto the value angle.
    pointer.setAttribute('transform', `rotate(${deg - 270} ${c} ${c})`);
    if (o.warm) pointer.style.stroke = tensionColor(n);
    readout.textContent = o.format ? o.format(param.value) : String(Math.round(param.value));
    dial.setAttribute('aria-valuenow', String(Math.round(param.value)));
    dial.setAttribute('aria-valuetext', o.describe ? o.describe(param.value) : String(Math.round(param.value)));
    root.classList.toggle('is-automated', param.automated);
  }

  // Vertical drag: 180 px = full range; Shift for fine adjust (1/6 speed). Double-click resets.
  dial.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    dial.focus();
    dial.setPointerCapture(e.pointerId);
    root.classList.add('is-dragging');
    param.dragStart();
    let lastY = e.clientY;
    let acc = norm();
    const move = (ev: PointerEvent) => {
      const dy = lastY - ev.clientY;
      lastY = ev.clientY;
      acc = clamp(acc + dy / (ev.shiftKey ? 1080 : 180), 0, 1);
      param.set(param.start + acc * (param.end - param.start));
    };
    const up = () => {
      dial.removeEventListener('pointermove', move);
      dial.removeEventListener('pointerup', up);
      dial.removeEventListener('pointercancel', up);
      root.classList.remove('is-dragging');
      param.dragEnd();
    };
    dial.addEventListener('pointermove', move);
    dial.addEventListener('pointerup', up);
    dial.addEventListener('pointercancel', up);
  });

  dial.addEventListener('dblclick', () => { param.dragStart(); param.reset(); param.dragEnd(); });

  dial.addEventListener('wheel', (e) => {
    e.preventDefault();
    const step = (param.end - param.start) / (e.shiftKey ? 400 : 100);
    param.set(param.value + (e.deltaY < 0 ? step : -step));
  }, { passive: false });

  dial.addEventListener('keydown', (e) => {
    const step = (param.end - param.start) / (e.shiftKey ? 100 : 20);
    const keys: Record<string, number> = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step, PageUp: step * 4, PageDown: -step * 4 };
    if (e.key in keys) { e.preventDefault(); e.stopPropagation(); param.set(param.value + keys[e.key]); }
    if (e.key === 'Home') { e.preventDefault(); param.set(param.start); }
    if (e.key === 'End') { e.preventDefault(); param.set(param.end); }
  });

  param.subscribe(draw);
  // Re-check the automation badge as it times out.
  setInterval(() => { if (root.classList.contains('is-automated') !== param.automated) draw(); }, 250);
  draw();
  return root;
}

export function ColorChips(toggles: ToggleParam[], names: string[], hints: string[]) {
  const root = el('div', { class: 'chips', role: 'group', 'aria-label': 'Color: allowed techniques' });
  root.innerHTML = toggles
    .map((_, i) => `<button class="chip" data-i="${i}" aria-pressed="false" title="${esc(hints[i])}"><i aria-hidden="true"></i>${esc(names[i])}</button>`)
    .join('');
  const buttons = [...root.querySelectorAll<HTMLButtonElement>('.chip')];
  root.addEventListener('click', (e) => {
    const b = (e.target as HTMLElement).closest<HTMLButtonElement>('.chip');
    if (!b) return;
    const t = toggles[Number(b.dataset.i)];
    t.set(!t.value);
  });
  const draw = () => buttons.forEach((b, i) => {
    b.setAttribute('aria-pressed', String(toggles[i].value));
    b.classList.toggle('is-automated', toggles[i].automated);
  });
  toggles.forEach((t) => t.subscribe(draw));
  draw();
  return root;
}

export function Segmented(param: ChoiceParam, label: string, onPedal?: (show: boolean) => void) {
  const root = el('div', { class: 'seg-ctl', role: 'radiogroup', 'aria-label': label });
  root.innerHTML = param.choices.map((c, i) => `<button role="radio" data-i="${i}" aria-checked="false">${esc(c)}</button>`).join('');
  const buttons = [...root.querySelectorAll<HTMLButtonElement>('button')];
  root.addEventListener('click', (e) => {
    const b = (e.target as HTMLElement).closest<HTMLButtonElement>('button');
    if (b) param.set(Number(b.dataset.i));
  });
  root.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    e.stopPropagation();
    const n = param.choices.length;
    param.set((param.value + (e.key === 'ArrowRight' ? 1 : n - 1)) % n);
    buttons[param.value].focus();
  });
  const draw = () => {
    buttons.forEach((b, i) => {
      b.setAttribute('aria-checked', String(i === param.value));
      b.tabIndex = i === param.value ? 0 : -1;
    });
    onPedal?.(param.choices[param.value] === 'Pedal');
  };
  param.subscribe(draw);
  draw();
  return root;
}

export function Switch(param: ToggleParam, label: string) {
  const root = el('button', { class: 'switch', role: 'switch', 'aria-checked': 'false' }, `<span class="switch-track"><i></i></span><span>${esc(label)}</span>`);
  root.addEventListener('click', () => param.set(!param.value));
  const draw = () => root.setAttribute('aria-checked', String(param.value));
  param.subscribe(draw);
  draw();
  return root;
}

export function Stepper(param: SliderParam, unit: string, label: string) {
  const root = el('div', { class: 'stepper', role: 'group', 'aria-label': label });
  root.innerHTML = `<button aria-label="Fewer ${esc(unit)}" data-d="-1">−</button><span class="num" aria-live="polite"></span><button aria-label="More ${esc(unit)}" data-d="1">+</button>`;
  const out = root.querySelector('span')!;
  root.addEventListener('click', (e) => {
    const b = (e.target as HTMLElement).closest<HTMLButtonElement>('button');
    if (b) param.set(param.value + Number(b.dataset.d));
  });
  const draw = () => { out.textContent = `${Math.round(param.value)} ${unit}`; };
  param.subscribe(draw);
  draw();
  return root;
}

const NOTE = ['C', 'C♯', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'];
export const noteName = (m: number) => `${NOTE[((m % 12) + 12) % 12]}${Math.floor(m / 12) - 1}`;

/** Compact piano strip C1–C7 with two draggable handles for the chord range; the bass range shows as a band. */
export function RangeControl(low: SliderParam, high: SliderParam) {
  const LO = 24, HI = 96; // C1..C7
  const root = el('div', { class: 'range', role: 'group', 'aria-label': 'Voicing range' });
  const whites: number[] = [];
  for (let m = LO; m <= HI; m++) if (![1, 3, 6, 8, 10].includes(m % 12)) whites.push(m);
  const wW = 100 / whites.length;
  const xOf = (m: number) => {
    const wi = whites.findIndex((w) => w >= m);
    const isBlack = [1, 3, 6, 8, 10].includes(m % 12);
    return isBlack ? wi * wW : wi * wW + wW / 2;
  };

  root.innerHTML = `
    <div class="range-keys" aria-hidden="true">
      ${whites.map((m) => `<i class="wk${m % 12 === 0 ? ' c' : ''}"></i>`).join('')}
      ${Array.from({ length: HI - LO + 1 }, (_, k) => LO + k).filter((m) => [1, 3, 6, 8, 10].includes(m % 12)).map((m) => `<i class="bk" style="left:${xOf(m)}%"></i>`).join('')}
      <span class="range-bass" style="left:${xOf(28)}%;width:${xOf(48) - xOf(28)}%" title="Bass range E1 to C3"></span>
      <span class="range-band"></span>
    </div>
    <button class="range-handle" data-h="low" role="slider" aria-label="Lowest chord note"></button>
    <button class="range-handle" data-h="high" role="slider" aria-label="Highest chord note"></button>
    <div class="range-readout num"><span data-r="low"></span><span class="muted">to</span><span data-r="high"></span></div>`;

  const keys = root.querySelector<HTMLElement>('.range-keys')!;
  const band = root.querySelector<HTMLElement>('.range-band')!;
  const hLow = root.querySelector<HTMLElement>('[data-h=low]')!;
  const hHigh = root.querySelector<HTMLElement>('[data-h=high]')!;
  const rLow = root.querySelector<HTMLElement>('[data-r=low]')!;
  const rHigh = root.querySelector<HTMLElement>('[data-r=high]')!;

  const draw = () => {
    const l = xOf(Math.round(low.value)), h = xOf(Math.round(high.value));
    band.style.left = `${l}%`;
    band.style.width = `${h - l}%`;
    hLow.style.left = `${l}%`;
    hHigh.style.left = `${h}%`;
    rLow.textContent = noteName(Math.round(low.value));
    rHigh.textContent = noteName(Math.round(high.value));
    hLow.setAttribute('aria-valuetext', rLow.textContent);
    hHigh.setAttribute('aria-valuetext', rHigh.textContent);
  };

  const bind = (handle: HTMLElement, p: SliderParam, other: () => number, isLow: boolean) => {
    handle.addEventListener('pointerdown', (e) => {
      handle.setPointerCapture(e.pointerId);
      p.dragStart();
      const move = (ev: PointerEvent) => {
        const rect = keys.getBoundingClientRect();
        const frac = clamp((ev.clientX - rect.left) / rect.width, 0, 1);
        const wi = Math.min(whites.length - 1, Math.floor(frac * whites.length));
        let m = whites[wi];
        m = isLow ? Math.min(m, other() - 12) : Math.max(m, other() + 12);
        p.set(m);
      };
      const up = () => { handle.removeEventListener('pointermove', move); handle.removeEventListener('pointerup', up); p.dragEnd(); };
      handle.addEventListener('pointermove', move);
      handle.addEventListener('pointerup', up);
    });
    handle.addEventListener('keydown', (e) => {
      const d = e.key === 'ArrowRight' || e.key === 'ArrowUp' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowDown' ? -1 : 0;
      if (!d) return;
      e.preventDefault();
      e.stopPropagation();
      const next = p.value + d * (e.shiftKey ? 1 : 12);
      p.set(isLow ? Math.min(next, other() - 12) : Math.max(next, other() + 12));
    });
  };
  bind(hLow, low, () => high.value, true);
  bind(hHigh, high, () => low.value, false);

  low.subscribe(draw);
  high.subscribe(draw);
  draw();
  return root;
}
