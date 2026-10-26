// One typed entry point for the bridge contract. Inside the plugin it calls the native functions
// registered by WebBridge.cpp; in a browser the mock backend answers with the same shapes.

import type { BackendEventName, BackendEvents, NativeFunctionName, NativeFunctions } from './contract';

export interface Backend {
  readonly native: boolean;
  call<K extends NativeFunctionName>(name: K, ...args: Parameters<NativeFunctions[K]>): Promise<ReturnType<NativeFunctions[K]> | undefined>;
  on<E extends BackendEventName>(name: E, fn: (payload: BackendEvents[E]) => void): () => void;
}

interface JuceWindow {
  __JUCE__?: {
    initialisationData?: { __juce__functions?: string[] };
    backend?: {
      addEventListener: (n: string, f: (p: unknown) => void) => unknown;
      removeEventListener?: (token: unknown) => void;
    };
  };
}

export function createNativeBackend(
  callNative: (name: string, ...args: unknown[]) => Promise<unknown>,
  registered: () => string[],
): Backend {
  return {
    native: true,
    async call(name, ...args) {
      // A function the integrator has not registered yet resolves to undefined, never throws.
      if (!registered().includes(name)) return undefined;
      return (await callNative(name, ...args)) as never;
    },
    on(name, fn) {
      const w = window as unknown as JuceWindow;
      const token = w.__JUCE__?.backend?.addEventListener(name, fn as (p: unknown) => void);
      return () => { if (token !== undefined) w.__JUCE__?.backend?.removeEventListener?.(token); };
    },
  };
}
