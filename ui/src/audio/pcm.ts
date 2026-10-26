// Pure PCM helpers for analyzePcm (no DOM, unit-tested). See docs/bridge-contract.md.

import { PCM_MAX_SECONDS, PCM_SAMPLE_RATE } from '../bridge/contract';

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

/** Bytes of float32 little-endian, independent of platform endianness. */
export function float32ToBytesLE(samples: Float32Array): Uint8Array {
  const bytes = new Uint8Array(samples.length * 4);
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < samples.length; i++) view.setFloat32(i * 4, samples[i], true);
  return bytes;
}

/** RFC 4648 base64 with padding. Pure JS so it behaves the same in every WebView and in Node;
    writes ASCII codes into one buffer, so 3 minutes of audio (16 MB) encodes in well under a second. */
export function bytesToBase64(bytes: Uint8Array): string {
  const n = bytes.length;
  const out = new Uint8Array(4 * Math.ceil(n / 3));
  const code = new Uint8Array(64);
  for (let i = 0; i < 64; i++) code[i] = B64.charCodeAt(i);
  let o = 0, i = 0;
  for (; i + 2 < n; i += 3) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out[o++] = code[(v >> 18) & 63]; out[o++] = code[(v >> 12) & 63]; out[o++] = code[(v >> 6) & 63]; out[o++] = code[v & 63];
  }
  if (n - i === 1) {
    const v = bytes[i] << 16;
    out[o++] = code[(v >> 18) & 63]; out[o++] = code[(v >> 12) & 63]; out[o++] = 61; out[o++] = 61;
  } else if (n - i === 2) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out[o++] = code[(v >> 18) & 63]; out[o++] = code[(v >> 12) & 63]; out[o++] = code[(v >> 6) & 63]; out[o++] = 61;
  }
  return new TextDecoder('ascii').decode(out);
}

export function base64ToBytes(b64: string): Uint8Array {
  const clean = b64.replace(/[^A-Za-z0-9+/]/g, '');
  const out = new Uint8Array(Math.floor((clean.length * 3) / 4));
  const lut = new Int16Array(128).fill(-1);
  for (let i = 0; i < B64.length; i++) lut[B64.charCodeAt(i)] = i;
  let o = 0;
  for (let i = 0; i < clean.length; i += 4) {
    const a = lut[clean.charCodeAt(i)], b = lut[clean.charCodeAt(i + 1)];
    const c = i + 2 < clean.length ? lut[clean.charCodeAt(i + 2)] : 0;
    const d = i + 3 < clean.length ? lut[clean.charCodeAt(i + 3)] : 0;
    const n = (a << 18) | (b << 12) | (c << 6) | d;
    if (o < out.length) out[o++] = (n >> 16) & 255;
    if (o < out.length) out[o++] = (n >> 8) & 255;
    if (o < out.length) out[o++] = n & 255;
  }
  return out;
}

export const encodePcmBase64 = (samples: Float32Array) => bytesToBase64(float32ToBytesLE(samples));

export function decodePcmBase64(b64: string): Float32Array {
  const bytes = base64ToBytes(b64);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = new Float32Array(Math.floor(bytes.length / 4));
  for (let i = 0; i < out.length; i++) out[i] = view.getFloat32(i * 4, true);
  return out;
}

/** Number of samples a base64 float32 payload holds, without decoding it. */
export function base64SampleCount(b64: string): number {
  const len = b64.length;
  const pad = b64.endsWith('==') ? 2 : b64.endsWith('=') ? 1 : 0;
  return Math.floor(((len * 3) / 4 - pad) / 4);
}

/** Frames kept from a source of `frames` at `rate` when capped at `maxSeconds`. */
export const cappedFrames = (frames: number, rate: number, maxSeconds = PCM_MAX_SECONDS) =>
  Math.min(frames, Math.floor(maxSeconds * rate));

/** Length of the OfflineAudioContext buffer that resamples `frames` at `fromRate` to `toRate`. */
export const resampledLength = (frames: number, fromRate: number, toRate = PCM_SAMPLE_RATE) =>
  Math.max(1, Math.ceil((frames * toRate) / fromRate));

/** Average of all channels (the Web Audio "speakers" downmix for mono output, minus its 1/sqrt2 for 5.1). */
export function downmix(channels: Float32Array[]): Float32Array {
  if (channels.length === 1) return channels[0];
  const n = Math.min(...channels.map((c) => c.length));
  const out = new Float32Array(n);
  for (const c of channels) for (let i = 0; i < n; i++) out[i] += c[i];
  const k = 1 / channels.length;
  for (let i = 0; i < n; i++) out[i] *= k;
  return out;
}

export const AUDIO_EXT = /\.(wav|wave|aif|aiff|flac|mp3|m4a|aac|ogg|caf)$/i;
export const isAudioFileName = (name: string) => AUDIO_EXT.test(name);
