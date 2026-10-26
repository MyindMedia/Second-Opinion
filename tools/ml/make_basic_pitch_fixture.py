#!/usr/bin/env python3
"""Synthesize the Basic Pitch fixture WAV and record the Python reference transcription.

Writes:
  tests/fixtures/basic_pitch_piano.wav             22,050 Hz mono 16-bit PCM, about 5 s
  tests/fixtures/basic_pitch_piano.expected.json   notes and posteriorgram summary from the
                                                   reference basic_pitch.inference.predict()

The audio is generated here from additive "piano-like" partials (no samples, no recordings), so
it is rights-free. The melody is the traditional "Mary Had a Little Lamb" (public domain) over a
C major / F major / G major / C major chord pattern.

The reference runs the ONNX model through onnxruntime, so TensorFlow is not needed:

  python3 -m venv .venv-ml && . .venv-ml/bin/activate
  pip install -r tools/ml/requirements.txt
  pip install --no-deps "basic-pitch==0.4.0"   # its TensorFlow pin has no wheels for new Pythons
  python tools/ml/make_basic_pitch_fixture.py

Deterministic: same inputs produce the same WAV bytes; the JSON depends only on the WAV, the model
and the onnxruntime version (recorded in the JSON).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import wave

import numpy as np

SR = 22050
REPO = pathlib.Path(__file__).resolve().parents[2]
MODEL = REPO / "models" / "basic_pitch" / "nmp.onnx"
WAV = REPO / "tests" / "fixtures" / "basic_pitch_piano.wav"
EXPECTED = REPO / "tests" / "fixtures" / "basic_pitch_piano.expected.json"

# (midi, start_sec, duration_sec, gain)
MELODY = [76, 74, 72, 74, 76, 76, 76, 74, 74, 76, 79, 79]
CHORDS = [
    ([48, 52, 55], 0.40, 1.15),  # C
    ([53, 57, 60], 1.60, 1.15),  # F
    ([55, 59, 62], 2.80, 1.15),  # G
    ([48, 52, 55], 4.00, 0.80),  # C
]


def piano_tone(midi: int, dur: float, gain: float, total: int, start: float) -> np.ndarray:
    """Additive tone: 8 slightly stretched partials, fast attack, per-partial exponential decay."""
    f0 = 440.0 * 2.0 ** ((midi - 69) / 12.0)
    n = int(round((dur + 0.08) * SR))
    t = np.arange(n, dtype=np.float64) / SR
    y = np.zeros(n)
    for h in range(1, 9):
        fh = f0 * h * np.sqrt(1.0 + 0.0004 * h * h)
        if fh >= 0.45 * SR:
            break
        decay = 1.8 + 0.9 * h
        y += (1.0 / h) * np.exp(-decay * t) * np.sin(2.0 * np.pi * fh * t)
    attack = np.minimum(1.0, t / 0.004)
    release_start = dur
    release = np.clip(1.0 - (t - release_start) / 0.06, 0.0, 1.0)
    y *= attack * release * gain
    out = np.zeros(total)
    s = int(round(start * SR))
    e = min(total, s + n)
    out[s:e] += y[: e - s]
    return out


def synthesize() -> np.ndarray:
    total = int(5.0 * SR)
    y = np.zeros(total)
    for i, m in enumerate(MELODY):
        y += piano_tone(m, 0.36, 0.55, total, 0.40 + 0.40 * i)
    for notes, start, dur in CHORDS:
        for m in notes:
            y += piano_tone(m, dur, 0.30, total, start)
    y *= 0.7 / np.max(np.abs(y))
    return np.round(y * 32767.0).astype(np.int16)


def write_wav(path: pathlib.Path, pcm: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.astype("<i2").tobytes())


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wav-only", action="store_true", help="only (re)write the WAV")
    args = ap.parse_args()

    write_wav(WAV, synthesize())
    print(f"wrote {WAV.relative_to(REPO)}")
    if args.wav_only:
        return

    import onnxruntime
    from basic_pitch import inference

    model_output, _midi, note_events = inference.predict(str(WAV), str(MODEL))
    note = model_output["note"]
    rows = [0, note.shape[0] // 4, note.shape[0] // 2, (3 * note.shape[0]) // 4, note.shape[0] - 1]
    notes = sorted(
        (
            {
                "pitch": int(p),
                "start": round(float(s), 6),
                "end": round(float(e), 6),
                "amplitude": round(float(a), 6),
            }
            for s, e, p, a, _bends in note_events
        ),
        key=lambda n: (n["start"], n["pitch"]),
    )
    data = {
        "generator": "tools/ml/make_basic_pitch_fixture.py",
        "wav": WAV.name,
        "sample_rate": SR,
        "onnxruntime": onnxruntime.__version__,
        "params": {
            "onset_threshold": inference.DEFAULT_ONSET_THRESHOLD,
            "frame_threshold": inference.DEFAULT_FRAME_THRESHOLD,
            "minimum_note_length_ms": inference.DEFAULT_MINIMUM_NOTE_LENGTH_MS,
            "melodia_trick": True,
        },
        "posteriorgram": {
            "frames": int(note.shape[0]),
            "pitches": int(note.shape[1]),
            "sum": round(float(np.sum(note, dtype=np.float64)), 4),
            "rows": {str(r): [round(float(v), 5) for v in note[r]] for r in rows},
        },
        "notes": notes,
    }
    EXPECTED.write_text(json.dumps(data, indent=1) + "\n")
    print(f"wrote {EXPECTED.relative_to(REPO)} with {len(notes)} notes")


if __name__ == "__main__":
    main()
