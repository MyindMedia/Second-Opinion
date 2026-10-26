// Browser-preview stand-in for the native DragStrip (src/bridge/NativeDragStrip.cpp).
// Inside the plugin this bar is a native JUCE component under the WebView, so it is not rendered here.
import { params } from '../bridge/juce';
import type { Store } from '../state/store';
import { el, icon } from '../ui/dom';

export function DragStripPreview(store: Store, onExport: () => void) {
  const root = el('footer', { class: 'dragstrip', 'aria-label': 'Drag MIDI to your DAW' });
  const lanes = ['Chords', 'Bass', 'Top line', 'All'];
  root.innerHTML = `
    ${lanes.map((l) => `<div class="drag-handle" draggable="true" data-lane="${l}" tabindex="0" aria-label="${l} MIDI, drag into your DAW">
        <span class="grip">${icon.grip()}</span><span class="dh-label" data-label="${l}">${l}</span><span class="dh-preview" aria-hidden="true"></span></div>`).join('')}
    <button class="btn btn-ghost export">Export MIDI…</button>
    <label class="preview-level"><span>Preview</span><input type="range" min="-60" max="0" step="0.5" aria-label="Preview level"/></label>`;

  const level = root.querySelector<HTMLInputElement>('input')!;
  level.addEventListener('input', () => params.previewLevel.set(Number(level.value)));
  params.previewLevel.subscribe((v) => { level.value = String(v); });
  level.value = String(params.previewLevel.value);

  root.addEventListener('dragstart', (e) => {
    const h = (e.target as HTMLElement).closest<HTMLElement>('.drag-handle');
    if (!h || h.classList.contains('is-disabled')) return e.preventDefault();
    h.classList.add('is-dragging');
    e.dataTransfer?.setData('text/plain', `SecondOpinion_Session_${h.dataset.lane}.mid`);
  });
  root.querySelector('.export')!.addEventListener('click', onExport);
  root.addEventListener('dragend', (e) => (e.target as HTMLElement).closest('.drag-handle')?.classList.remove('is-dragging'));

  // Mirrors whatever was last sent with setProgression (a suggestion, a section or the whole song).
  const render = () => {
    const s = store.get();
    const d = s.dragPreview;
    root.querySelectorAll<HTMLElement>('.drag-handle').forEach((h) => {
      h.classList.toggle('is-disabled', !d);
      h.title = d ? `Drag into your DAW: ${d.title}` : s.mode === 'build' ? 'Build a song first' : 'Pick a suggestion first';
      const p = h.querySelector<HTMLElement>('.dh-preview')!;
      const span = d ? Math.min(d.totalBeats, s.mode === 'build' ? d.totalBeats : 32) : 1;
      p.innerHTML = d
        ? d.chords.filter((c) => c.startBeat - d.startBeat < span).map((c, i) => {
            const x = c.startBeat - d.startBeat;
            return `<i style="left:${(x / span) * 100}%;width:${Math.max(1, (c.beats / span) * 100 - 1)}%;bottom:${h.dataset.lane === 'Bass' ? 2 + (i % 3) * 2 : 4 + ((i * 5) % 9)}px"></i>`;
          }).join('')
        : '';
      h.classList.toggle('lane-bass', h.dataset.lane === 'Bass');
    });
    (root.querySelector('.export') as HTMLButtonElement).disabled = !d;
  };
  store.subscribe((_, c) => { if (c.has('dragPreview') || c.has('mode')) render(); });
  render();
  return root;
}
