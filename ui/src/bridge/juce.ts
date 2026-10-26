// Bridge to the JUCE backend. Every control goes through here so the same UI works:
//   - inside the plugin: bound to host parameters through JUCE's WebView relays (automation both ways)
//   - in a normal browser: local state with the same defaults (the brief's "mock the bridge")
import * as Juce from '@juce-framework/webview';

type Source = 'user' | 'host';
type Listener<T> = (value: T, source: Source) => void;

interface JuceInit {
  __juce__sliders: string[];
  __juce__toggles: string[];
  __juce__comboBoxes: string[];
  __juce__functions: string[];
}

const init = (): JuceInit => {
  const w = window as unknown as { __JUCE__?: { initialisationData?: Partial<JuceInit> } };
  const d = w.__JUCE__?.initialisationData ?? {};
  return {
    __juce__sliders: d.__juce__sliders ?? [],
    __juce__toggles: d.__juce__toggles ?? [],
    __juce__comboBoxes: d.__juce__comboBoxes ?? [],
    __juce__functions: d.__juce__functions ?? [],
  };
};

export const isNative = () => init().__juce__functions.length > 0;

/** An echo of our own write comes back from the backend within this window; anything else is the host. */
const ECHO_MS = 400;
const AUTOMATION_BADGE_MS = 1500;

abstract class Param<T> {
  protected listeners = new Set<Listener<T>>();
  protected lastLocalWrite = 0;
  automatedUntil = 0;
  abstract get value(): T;
  abstract readonly native: boolean;

  subscribe(fn: Listener<T>) {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  get automated() { return performance.now() < this.automatedUntil; }

  protected emit(source: Source) {
    if (source === 'host') this.automatedUntil = performance.now() + AUTOMATION_BADGE_MS;
    for (const l of this.listeners) l(this.value, source);
  }

  protected sourceOfIncoming(): Source {
    return performance.now() - this.lastLocalWrite < ECHO_MS ? 'user' : 'host';
  }
}

export interface SliderSpec { start: number; end: number; interval: number; defaultValue: number; }

export class SliderParam extends Param<number> {
  readonly native: boolean;
  private state?: ReturnType<typeof Juce.getSliderState>;
  private local: number;

  constructor(readonly id: string, readonly spec: SliderSpec) {
    super();
    this.local = spec.defaultValue;
    this.native = init().__juce__sliders.includes(id);
    if (this.native) {
      this.state = Juce.getSliderState(id);
      this.state.valueChangedEvent.addListener(() => this.emit(this.sourceOfIncoming()));
      this.state.propertiesChangedEvent.addListener(() => this.emit('user'));
    }
  }

  get start() { return this.state ? this.state.properties.start : this.spec.start; }
  get end() { return this.state ? this.state.properties.end : this.spec.end; }

  get value() { return this.state ? this.state.getScaledValue() : this.local; }

  set(value: number) {
    const v = Math.min(this.end, Math.max(this.start, value));
    this.lastLocalWrite = performance.now();
    if (this.state) {
      const range = this.state.properties.end - this.state.properties.start;
      this.state.setNormalisedValue(Math.pow((v - this.state.properties.start) / range, this.state.properties.skew));
    } else {
      const step = this.spec.interval;
      this.local = step > 0 ? Math.round(v / step) * step : v;
    }
    this.emit('user');
  }

  reset() { this.set(this.spec.defaultValue); }
  dragStart() { this.state?.sliderDragStarted(); }
  dragEnd() { this.state?.sliderDragEnded(); }

  /** Host automation simulator for the browser preview (lets you see the "A" badge). */
  simulateHost(value: number) {
    if (this.native) return;
    this.local = value;
    this.emit('host');
  }
}

export class ToggleParam extends Param<boolean> {
  readonly native: boolean;
  private state?: ReturnType<typeof Juce.getToggleState>;
  private local: boolean;

  constructor(readonly id: string, defaultValue: boolean) {
    super();
    this.local = defaultValue;
    this.native = init().__juce__toggles.includes(id);
    if (this.native) {
      this.state = Juce.getToggleState(id);
      this.state.valueChangedEvent.addListener(() => this.emit(this.sourceOfIncoming()));
    }
  }

  get value() { return this.state ? this.state.getValue() : this.local; }

  set(v: boolean) {
    this.lastLocalWrite = performance.now();
    if (this.state) this.state.setValue(v);
    else this.local = v;
    this.emit('user');
  }
}

export class ChoiceParam extends Param<number> {
  readonly native: boolean;
  private state?: ReturnType<typeof Juce.getComboBoxState>;
  private local: number;

  constructor(readonly id: string, readonly choices: string[], defaultIndex: number) {
    super();
    this.local = defaultIndex;
    this.native = init().__juce__comboBoxes.includes(id);
    if (this.native) {
      this.state = Juce.getComboBoxState(id);
      this.state.valueChangedEvent.addListener(() => this.emit(this.sourceOfIncoming()));
    }
  }

  get value() { return this.state ? this.state.getChoiceIndex() : this.local; }

  set(i: number) {
    this.lastLocalWrite = performance.now();
    if (this.state) this.state.setChoiceIndex(i);
    else this.local = i;
    this.emit('user');
  }
}

// Parameter IDs must match src/plugin/Parameters.h.
export const params = {
  tension: new SliderParam('tension', { start: 0, end: 100, interval: 1, defaultValue: 35 }),
  smoothness: new SliderParam('smoothness', { start: 0, end: 100, interval: 1, defaultValue: 70 }),
  density: new SliderParam('density', { start: 3, end: 6, interval: 1, defaultValue: 4 }),
  rangeLow: new SliderParam('rangeLow', { start: 24, end: 96, interval: 1, defaultValue: 36 }),
  rangeHigh: new SliderParam('rangeHigh', { start: 24, end: 96, interval: 1, defaultValue: 84 }),
  previewLevel: new SliderParam('previewLevel', { start: -60, end: 0, interval: 0.1, defaultValue: -12 }),
  bassMotion: new ChoiceParam('bassMotion', ['Root', 'Inversions', 'Walking', 'Pedal'], 1),
  melodyOnTop: new ToggleParam('melodyOnTop', true),
  colors: [
    new ToggleParam('colorDiatonic', true),
    new ToggleParam('colorBorrowed', true),
    new ToggleParam('colorSecondary', true),
    new ToggleParam('colorTritone', false),
    new ToggleParam('colorMediant', false),
    new ToggleParam('colorNeoRiemann', false),
  ],
};

// ---- Native functions (PRD §7.9). Only called when the backend registered them. ----

export async function callNative<T>(name: string, ...args: unknown[]): Promise<T | undefined> {
  if (!init().__juce__functions.includes(name)) return undefined;
  return (await Juce.getNativeFunction(name)(...args)) as T;
}

export function onBackendEvent(name: string, fn: (payload: any) => void) {
  const w = window as unknown as { __JUCE__?: { backend?: { addEventListener: (n: string, f: (p: unknown) => void) => void } } };
  w.__JUCE__?.backend?.addEventListener(name, fn);
}
