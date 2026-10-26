// Decodes a dropped audio file in the WebView for analyzePcm: decodeAudioData, then an
// OfflineAudioContext downmixes to mono and resamples to 22050 Hz. Capped at 3 minutes.
// No realtime AudioContext is created, so nothing opens an audio device inside the plugin.

import { PCM_MAX_SECONDS, PCM_SAMPLE_RATE } from '../bridge/contract';
import { cappedFrames, downmix, resampledLength } from './pcm';

export interface DecodedPcm {
  samples: Float32Array;
  sampleRate: number;
  seconds: number;           // after capping
  sourceSeconds: number;     // full length of the file
  truncated: boolean;
}

type OfflineCtor = typeof OfflineAudioContext;

function offlineCtor(): OfflineCtor | undefined {
  const w = window as unknown as { OfflineAudioContext?: OfflineCtor; webkitOfflineAudioContext?: OfflineCtor };
  return w.OfflineAudioContext ?? w.webkitOfflineAudioContext;
}

function decode(ctx: OfflineAudioContext, data: ArrayBuffer): Promise<AudioBuffer> {
  // Callback form as well, for older WebKit that returns undefined instead of a promise.
  return new Promise((resolve, reject) => {
    const p = ctx.decodeAudioData(data, resolve, reject) as Promise<AudioBuffer> | undefined;
    p?.then(resolve, reject);
  });
}

export async function decodeToMono(file: Blob, onStep?: (pct: number) => void): Promise<DecodedPcm> {
  const Ctor = offlineCtor();
  if (!Ctor) throw new Error('This view cannot decode audio. Use Open audio file instead.');
  onStep?.(10);
  const data = await file.arrayBuffer();
  onStep?.(30);
  const audio = await decode(new Ctor(1, 1, 44100), data);
  onStep?.(65);

  const frames = cappedFrames(audio.length, audio.sampleRate, PCM_MAX_SECONDS);
  const length = resampledLength(frames, audio.sampleRate, PCM_SAMPLE_RATE);
  let samples: Float32Array;
  try {
    const off = new Ctor(1, length, PCM_SAMPLE_RATE);
    const src = off.createBufferSource();
    src.buffer = audio;
    src.connect(off.destination);    // mono destination: Web Audio downmixes the channels
    src.start(0, 0, frames / audio.sampleRate);
    samples = (await off.startRendering()).getChannelData(0);
  } catch {
    // Fallback: average channels at the decoded rate and let C++ resample (sampleRate is reported).
    const chans = Array.from({ length: audio.numberOfChannels }, (_, i) => audio.getChannelData(i).subarray(0, frames));
    const mono = downmix(chans);
    onStep?.(90);
    return { samples: mono, sampleRate: audio.sampleRate, seconds: frames / audio.sampleRate, sourceSeconds: audio.duration, truncated: frames < audio.length };
  }
  onStep?.(90);
  return {
    samples,
    sampleRate: PCM_SAMPLE_RATE,
    seconds: samples.length / PCM_SAMPLE_RATE,
    sourceSeconds: audio.duration,
    truncated: frames < audio.length,
  };
}
