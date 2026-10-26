import { params } from '../bridge/juce';
import type { Store } from '../state/store';
import { el, icon } from '../ui/dom';
import { ColorChips, Knob, RangeControl, Segmented, Stepper, Switch } from './controls';

export interface RailActions {
  generate(): void;
  shuffle(): void;
}

const COLOR_NAMES = ['Diatonic', 'Borrowed', 'Secondary', 'Tritone sub', 'Mediant', 'Neo-Riemann'];
const COLOR_HINTS = [
  'Chords from the key itself',
  'Modal interchange: chords from the parallel major or minor',
  'Secondary dominants that point at chords other than home',
  'A dominant swapped for the one a tritone away',
  'Chromatic mediants: chords a third apart with a surprising shared note',
  'Neo-Riemannian: one-note moves between major and minor triads (P, L, R)',
];

export function ControlRail(store: Store, actions: RailActions) {
  const root = el('section', { class: 'rail', 'aria-label': 'Suggestion controls' });

  const tension = Knob(params.tension, {
    size: 72, label: 'Tension', warm: true, captions: ['Safe', 'Spicy', 'Colorful'],
    describe: (v) => `${Math.round(v)}, ${v < 34 ? 'safe' : v < 67 ? 'colorful' : 'spicy'}`,
  });
  const sparkWrap = el('div', { class: 'spark-wrap', title: 'Average tension of each generated set' },
    `<svg class="spark" width="44" height="22" viewBox="0 0 44 22" aria-hidden="true"></svg><span class="spark-cap">set avg</span>`);
  const spark = sparkWrap.querySelector('svg')!;

  const smooth = Knob(params.smoothness, { size: 52, label: 'Voice-leading', captions: ['Leapy', 'Smooth'] });

  const pedal = el('label', { class: 'pedal-pick', hidden: true }, `<span class="sr-only">Pedal note</span><select>${['F', 'C', 'A♭', 'E♭', 'B♭', 'D♭'].map((n) => `<option>${n}</option>`).join('')}</select>`);
  const bass = Segmented(params.bassMotion, 'Bass motion', (show) => { pedal.hidden = !show; });

  const melodyMode = el('label', { class: 'mini-select' }, `<span class="sr-only">Melody position</span><select><option>Exact</option><option>3rd below</option><option>6th below</option></select>`);

  root.innerHTML = `
    <div class="rail-group g-tension"></div>
    <div class="rail-group g-smooth"></div>
    <div class="rail-sep" aria-hidden="true"></div>
    <div class="rail-group g-color"><div class="rail-label">Color</div></div>
    <div class="rail-sep" aria-hidden="true"></div>
    <div class="rail-group g-bass">
      <div class="rail-label">Bass motion</div>
      <div class="rail-row" data-slot="bass"></div>
      <div class="rail-label">Melody on top</div>
      <div class="rail-row" data-slot="melody"></div>
    </div>
    <div class="rail-group g-range">
      <div class="rail-label">Range</div>
      <div data-slot="range"></div>
      <div class="rail-row"><span class="rail-label inline">Density</span><span data-slot="density"></span></div>
    </div>
    <div class="rail-group g-go">
      <button class="btn btn-primary" data-act="generate">${icon.sparkle()}<span>Generate</span><kbd>R</kbd></button>
      <button class="btn btn-secondary" data-act="shuffle">${icon.shuffle()}<span>Shuffle</span></button>
    </div>`;

  const q = (s: string) => root.querySelector<HTMLElement>(s)!;
  q('.g-tension').append(tension, sparkWrap);
  q('.g-smooth').append(smooth);
  q('.g-color').append(ColorChips(params.colors, COLOR_NAMES, COLOR_HINTS));
  q('[data-slot=bass]').append(bass, pedal);
  q('[data-slot=melody]').append(Switch(params.melodyOnTop, 'On'), melodyMode);
  q('[data-slot=range]').append(RangeControl(params.rangeLow, params.rangeHigh));
  q('[data-slot=density]').append(Stepper(params.density, 'notes', 'Density'));

  const gen = q('[data-act=generate]') as HTMLButtonElement;
  gen.addEventListener('click', () => actions.generate());
  q('[data-act=shuffle]').addEventListener('click', () => actions.shuffle());

  params.melodyOnTop.subscribe((v) => { (melodyMode.querySelector('select') as HTMLSelectElement).disabled = !v; });

  function drawSpark() {
    const s = store.get();
    const sets = [...s.history.slice(-9), s.suggestions].filter((x) => x.length);
    const avgs = sets.map((set) => set.reduce((a, b) => a + b.tension, 0) / set.length);
    if (!avgs.length) { spark.innerHTML = ''; return; }
    const pts = avgs.map((v, i) => `${avgs.length === 1 ? 22 : 2 + (i * 40) / (avgs.length - 1)},${20 - (v / 100) * 18}`);
    const last = pts[pts.length - 1].split(',');
    spark.innerHTML = `<polyline points="${pts.join(' ')}" /><circle cx="${last[0]}" cy="${last[1]}" r="2"/>`;
  }

  store.subscribe((s, changed) => {
    if (changed.has('generating')) {
      gen.classList.toggle('is-busy', s.generating);
      gen.disabled = s.generating || !s.analysis;
      gen.querySelector('span')!.textContent = s.generating ? 'Writing…' : 'Generate';
    }
    if (changed.has('suggestions') || changed.has('history')) drawSpark();
    if (changed.has('phase')) gen.disabled = s.phase !== 'ready';
  });
  drawSpark();
  return root;
}
