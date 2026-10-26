export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Record<string, string | number | boolean | undefined> = {},
  html = '',
): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === false) continue;
    e.setAttribute(k, v === true ? '' : String(v));
  }
  if (html) e.innerHTML = html;
  return e;
}

export const esc = (s: string) =>
  s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!);

export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

/** Mixes the two tension token colors (cool #5B8CFF → warm #FF5E3A) for t in 0..1. */
export function tensionColor(t: number) {
  const a = [0x5b, 0x8c, 0xff];
  const b = [0xff, 0x5e, 0x3a];
  const m = a.map((v, i) => Math.round(v + (b[i] - v) * clamp(t, 0, 1)));
  return `rgb(${m[0]}, ${m[1]}, ${m[2]})`;
}

// Stroke icons, 16px grid, currentColor. Drawn for this UI (no icon font, no network).
const svg = (body: string, size = 16) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;

export const icon = {
  record: (size = 12) => `<svg width="${size}" height="${size}" viewBox="0 0 12 12" aria-hidden="true"><circle cx="6" cy="6" r="5" fill="currentColor"/></svg>`,
  stop: () => `<svg width="12" height="12" viewBox="0 0 12 12" aria-hidden="true"><rect x="1.5" y="1.5" width="9" height="9" rx="1.5" fill="currentColor"/></svg>`,
  play: (size = 14) => `<svg width="${size}" height="${size}" viewBox="0 0 14 14" aria-hidden="true"><path d="M4 2.6v8.8a.6.6 0 0 0 .9.5l7-4.4a.6.6 0 0 0 0-1L4.9 2.1a.6.6 0 0 0-.9.5z" fill="currentColor"/></svg>`,
  pause: (size = 14) => `<svg width="${size}" height="${size}" viewBox="0 0 14 14" aria-hidden="true"><rect x="3" y="2.5" width="3" height="9" rx="1" fill="currentColor"/><rect x="8" y="2.5" width="3" height="9" rx="1" fill="currentColor"/></svg>`,
  star: (filled: boolean) => svg(`<path d="M8 1.8l1.9 3.9 4.3.6-3.1 3 .7 4.3L8 11.6l-3.8 2 .7-4.3-3.1-3 4.3-.6z" ${filled ? 'fill="currentColor"' : ''}/>`),
  lock: () => svg('<rect x="3" y="7" width="10" height="7" rx="1.5"/><path d="M5.5 7V5a2.5 2.5 0 0 1 5 0v2"/>', 12),
  pencil: () => svg('<path d="M10.5 2.5l3 3L6 13H3v-3z"/>', 10),
  gear: () => svg('<circle cx="8" cy="8" r="2.2"/><path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4"/>'),
  undo: () => svg('<path d="M5 4L2 7l3 3"/><path d="M2 7h7.5a4 4 0 0 1 0 8H7"/>'),
  redo: () => svg('<path d="M11 4l3 3-3 3"/><path d="M14 7H6.5a4 4 0 0 0 0 8H9"/>'),
  close: () => svg('<path d="M4 4l8 8M12 4l-8 8"/>'),
  chevron: () => svg('<path d="M4 6l4 4 4-4"/>', 12),
  shuffle: () => svg('<path d="M2 4h2.5c3 0 4 8 7 8H14M12 10l2 2-2 2M2 12h2.5c1.2 0 2-1.2 2.6-2.6M9.4 6.6C10 5.2 10.8 4 12 4h2M12 2l2 2-2 2"/>'),
  sparkle: () => svg('<path d="M8 2v3M8 11v3M2 8h3M11 8h3M4 4l1.8 1.8M10.2 10.2L12 12M4 12l1.8-1.8M10.2 5.8L12 4"/>'),
  drop: () => svg('<path d="M8 2v8M4.5 6.5L8 10l3.5-3.5"/><path d="M2.5 11v1.5a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1V11"/>', 16),
  info: () => svg('<circle cx="8" cy="8" r="6"/><path d="M8 7.5v3.5M8 5h.01"/>', 14),
  panel: () => svg('<rect x="2" y="3" width="12" height="10" rx="1.5"/><path d="M10 3v10"/>'),
  grip: () => `<svg width="8" height="12" viewBox="0 0 8 12" aria-hidden="true">${[0, 1, 2].map((r) => [0, 1].map((c) => `<circle cx="${2 + c * 4}" cy="${2 + r * 4}" r="1" fill="currentColor"/>`).join('')).join('')}</svg>`,
  minus: () => svg('<path d="M4 8h8"/>', 12),
  plus: () => svg('<path d="M4 8h8M8 4v8"/>', 12),
};
