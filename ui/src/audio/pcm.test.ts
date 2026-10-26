import { describe, expect, it } from 'vitest';
import { PCM_MAX_SECONDS, PCM_SAMPLE_RATE } from '../bridge/contract';
import {
  base64SampleCount, base64ToBytes, bytesToBase64, cappedFrames, decodePcmBase64, downmix, encodePcmBase64, float32ToBytesLE,
  isAudioFileName, resampledLength,
} from './pcm';

describe('base64 PCM encoder', () => {
  it('matches Node base64 for every padding case', () => {
    for (const n of [0, 1, 2, 3, 4, 5, 100, 49152, 49153, 49154, 131071]) {
      const bytes = new Uint8Array(n).map((_, i) => (i * 37 + 11) & 255);
      let bin = '';
      for (const b of bytes) bin += String.fromCharCode(b);
      expect(bytesToBase64(bytes)).toBe(btoa(bin));
      expect(Array.from(base64ToBytes(bytesToBase64(bytes)))).toEqual(Array.from(bytes));
    }
  });

  it('writes float32 little-endian', () => {
    const bytes = float32ToBytesLE(new Float32Array([1, -0.5]));
    expect(Array.from(bytes)).toEqual([0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0xbf]);
  });

  it('round-trips samples exactly and reports the sample count without decoding', () => {
    const s = new Float32Array(22050 + 3).map((_, i) => Math.sin(i / 7) * 0.8);
    const b64 = encodePcmBase64(s);
    expect(base64SampleCount(b64)).toBe(s.length);
    expect(decodePcmBase64(b64)).toEqual(s);
  });

  it('handles a 3 minute payload', () => {
    const n = PCM_SAMPLE_RATE * PCM_MAX_SECONDS;
    const b64 = encodePcmBase64(new Float32Array(n));
    expect(b64.length).toBe(4 * Math.ceil((n * 4) / 3));
    expect(base64SampleCount(b64)).toBe(n);
  });
});

describe('resample length math', () => {
  it('maps common rates to 22050 Hz', () => {
    expect(resampledLength(44100, 44100)).toBe(22050);
    expect(resampledLength(48000, 48000)).toBe(22050);
    expect(resampledLength(96000 * 10, 96000)).toBe(220500);
    expect(resampledLength(1, 48000)).toBe(1);
    expect(resampledLength(48001, 48000)).toBe(22051); // rounds up so no tail is lost
  });

  it('caps long files at 3 minutes before resampling', () => {
    expect(cappedFrames(44100 * 240, 44100)).toBe(44100 * 180);
    expect(cappedFrames(44100 * 60, 44100)).toBe(44100 * 60);
    expect(resampledLength(cappedFrames(48000 * 300, 48000), 48000)).toBe(22050 * 180);
  });

  it('downmixes by averaging channels', () => {
    expect(Array.from(downmix([new Float32Array([1, 0, 0.5]), new Float32Array([0, 1, 0.5])]))).toEqual([0.5, 0.5, 0.5]);
    const mono = new Float32Array([0.25]);
    expect(downmix([mono])).toBe(mono);
  });

  it('recognizes audio file names', () => {
    expect(isAudioFileName('Vocal take 3.WAV')).toBe(true);
    expect(isAudioFileName('loop.aiff')).toBe(true);
    expect(isAudioFileName('notes.txt')).toBe(false);
  });
});
