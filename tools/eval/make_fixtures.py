#!/usr/bin/env python3
"""Synthesises the rights-free chord evaluation set (PRD 8, P2-05).

Every clip is generated here from explicit seeds, so the WAVs and labels are fully reproducible
and carry no third-party rights. Timbres aim to be realistic enough to stress the analysis:

- additive piano with string inharmonicity, per-partial decay, two detuned strings, hammer noise
- FM electric piano with a decaying tine
- detuned band-limited saw pad, low-passed, slow attack
- plucked guitar (Karplus-Strong with fractional delay) strummed with string offsets
- sung-vowel melody: glottal-like source through vowel formants, vibrato and portamento
- picked electric bass, light drums (kick, snare, hats) and a small Schroeder reverb

Usage: python3 tools/eval/make_fixtures.py [--out tests/fixtures/eval]
Writes <name>.wav (22,050 Hz, mono, 16-bit) and <name>.json (PRD 8 label format) per clip.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import wave

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chordlib  # noqa: E402

SR = 22050


def midi_hz(m: float, cents: float) -> float:
    return 440.0 * 2.0 ** ((m - 69.0) / 12.0 + cents / 1200.0)


# ---- instruments --------------------------------------------------------------------------------
# Each returns a mono float64 array of `dur` seconds (plus a release tail) for one note.

def piano(m, dur, vel, cents, rng):
    f0 = midi_hz(m, cents)
    rel = 0.12
    n = int((dur + rel) * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    b = 1.5e-4 * 2.0 ** ((m - 48) / 18.0)            # inharmonicity grows up the keyboard
    tau0 = 3.0 * 2.0 ** (-(m - 48) / 20.0)
    bright = 0.6 + 0.6 * vel
    for h in range(1, 25):
        fh = h * f0 * np.sqrt(1.0 + b * h * h)
        if fh > 0.45 * SR:
            break
        amp = (1.0 / h ** (1.6 - 0.5 * bright))
        tau = tau0 / (1.0 + 0.35 * (h - 1))
        for detune in (-0.6, 0.6):                    # two strings per note
            fd = fh * 2.0 ** (detune / 1200.0)
            ph = rng.uniform(0, 2 * np.pi)
            out += 0.5 * amp * np.exp(-t / tau) * np.sin(2 * np.pi * fd * t + ph)
    # hammer: short low-passed noise burst
    k = int(0.006 * SR)
    noise = rng.standard_normal(k) * np.hanning(2 * k)[k:]
    bb, aa = signal.butter(2, min(0.99, 3 * f0 / (SR / 2)))
    out[:k] += 0.15 * signal.lfilter(bb, aa, noise)
    env = np.ones(n)
    a = int(0.003 * SR)
    env[:a] = np.linspace(0, 1, a)
    r0 = int(dur * SR)
    env[r0:] = np.exp(-np.arange(n - r0) / (0.03 * SR))   # damper
    return vel * out * env


def epiano(m, dur, vel, cents, rng):
    f0 = midi_hz(m, cents)
    rel = 0.2
    n = int((dur + rel) * SR)
    t = np.arange(n) / SR
    index = (0.8 + 1.8 * vel) * np.exp(-t / 0.25)
    ph = rng.uniform(0, 2 * np.pi)
    y = np.sin(2 * np.pi * f0 * t + ph + index * np.sin(2 * np.pi * f0 * t))
    if 14 * f0 < 0.45 * SR:
        y += 0.12 * vel * np.exp(-t / 0.04) * np.sin(2 * np.pi * 14 * f0 * t)
    env = np.exp(-t / 1.6)
    a = int(0.004 * SR)
    env[:a] *= np.linspace(0, 1, a)
    r0 = int(dur * SR)
    env[r0:] *= np.exp(-np.arange(n - r0) / (0.06 * SR))
    return vel * y * env


def pad(m, dur, vel, cents, rng):
    f0 = midi_hz(m, cents)
    rel = 0.5
    n = int((dur + rel) * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for det in (-7.0, 0.0, 6.0):
        fd = f0 * 2.0 ** (det / 1200.0)
        ph = rng.uniform(0, 2 * np.pi, 40)
        for h in range(1, 40):
            if h * fd > 0.45 * SR:
                break
            y += np.sin(2 * np.pi * h * fd * t + ph[h]) / h
    bb, aa = signal.butter(2, 1800.0 / (SR / 2))
    y = signal.lfilter(bb, aa, y) / 3.0
    env = np.minimum(1.0, t / 0.25)
    r0 = int(dur * SR)
    env[r0:] *= np.exp(-np.arange(n - r0) / (0.15 * SR))
    return vel * 0.5 * y * env


def guitar(m, dur, vel, cents, rng):
    """Karplus-Strong: y[n] = x[n] + g * (loop filter) y[n - M ...], fractional delay by linear
    interpolation, so the loop delay is exactly SR / f0."""
    f0 = midi_hz(m, cents)
    rel = 0.08
    n = int((dur + rel) * SR)
    d = SR / f0 - 0.5                     # the two-point average adds half a sample
    mi = int(np.floor(d))
    fr = d - mi
    g = 0.996 ** (220.0 / max(f0, 60.0))   # longer sustain for low strings
    interp = np.array([1.0 - fr, fr])
    loop = np.convolve(interp, [0.5, 0.5]) * g
    a = np.zeros(mi + 3)
    a[0] = 1.0
    a[mi:mi + 3] -= loop
    burst = rng.uniform(-1, 1, mi + 1)
    burst = signal.lfilter([0.6, 0.4], [1.0], burst)   # softer pick
    x = np.zeros(n)
    x[:len(burst)] = burst
    y = signal.lfilter([1.0], a, x)
    r0 = int(dur * SR)
    y[r0:] *= np.exp(-np.arange(n - r0) / (0.02 * SR))
    return vel * 0.7 * y


def synth_bass(m, dur, vel, cents, rng):
    """Bright saw bass through a 2-pole low-pass at 1.4 kHz: strong 2nd and 3rd harmonics."""
    f0 = midi_hz(m, cents)
    rel = 0.05
    n = int((dur + rel) * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for h in range(1, 40):
        if h * f0 > 0.45 * SR:
            break
        y += np.sin(2 * np.pi * h * f0 * t + rng.uniform(0, 2 * np.pi)) / h
    bb, aa = signal.butter(2, 1400.0 / (SR / 2))
    y = signal.lfilter(bb, aa, y) * np.exp(-t / 0.9)
    a = int(0.004 * SR)
    y[:a] *= np.linspace(0, 1, a)
    r0 = int(dur * SR)
    y[r0:] *= np.exp(-np.arange(n - r0) / (0.015 * SR))
    return vel * 0.8 * y


def drive_bass(m, dur, vel, cents, rng):
    """Overdriven bass (tanh on a saw, then a 2.5 kHz low-pass): odd harmonics far above the 1/h
    model, so its 3rd partial (a 12th up) reads like a real fifth in the chord register."""
    y = synth_bass(m, dur, 1.0, cents, rng)
    y = np.tanh(3.0 * y / (np.max(np.abs(y)) + 1e-9))
    bb, aa = signal.butter(2, 2500.0 / (SR / 2))
    return vel * 0.6 * signal.lfilter(bb, aa, y)


def bass(m, dur, vel, cents, rng):
    f0 = midi_hz(m, cents)
    rel = 0.06
    n = int((dur + rel) * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for h in range(1, 9):
        fh = h * f0 * np.sqrt(1.0 + 2e-5 * h * h)
        y += np.exp(-t * (1.0 + 0.8 * h)) * np.sin(2 * np.pi * fh * t + rng.uniform(0, 2 * np.pi)) / h ** 1.3
    a = int(0.005 * SR)
    y[:a] *= np.linspace(0, 1, a)
    r0 = int(dur * SR)
    y[r0:] *= np.exp(-np.arange(n - r0) / (0.015 * SR))
    return vel * y


VOWELS = {   # formant frequencies (Hz) and bandwidths
    "a": ((750, 1150, 2600), (90, 110, 160)),
    "o": ((500, 850, 2500), (80, 100, 150)),
    "e": ((450, 1950, 2600), (70, 110, 160)),
    "u": ((350, 700, 2450), (70, 90, 150)),
}


def formant_gain(f, vowel):
    fs, bws = VOWELS[vowel]
    g = np.zeros_like(f)
    for fc, bw, w in zip(fs, bws, (1.0, 0.6, 0.25)):
        g += w / np.sqrt(1.0 + ((f - fc) / (bw / 2)) ** 2)
    return g + 0.02


def sing(notes, beat_sec, start, total_n, cents, rng):
    """notes: list of (midi or None, beats). A continuous voice with 40 ms portamento, vibrato
    (5.5 Hz, 25 cents, fading in after 150 ms) and a vowel per note."""
    out = np.zeros(total_n)
    t_cursor = start
    vowels = list(VOWELS)
    for idx, (m, beats) in enumerate(notes):
        dur = beats * beat_sec
        if m is None:
            t_cursor += dur
            continue
        n = int(dur * 0.92 * SR)
        t = np.arange(n) / SR
        prev = notes[idx - 1][0] if idx > 0 and notes[idx - 1][0] is not None else m
        glide = prev + (m - prev) * np.minimum(1.0, t / 0.04)
        vib = 0.25 * np.sin(2 * np.pi * 5.5 * t) * np.clip((t - 0.15) / 0.2, 0, 1)
        f0 = 440.0 * 2.0 ** ((glide + vib - 69.0) / 12.0 + cents / 1200.0)
        phase = 2 * np.pi * np.cumsum(f0) / SR
        vowel = vowels[int(rng.integers(len(vowels)))]
        y = np.zeros(n)
        for h in range(1, 30):
            fh = h * f0
            if fh.max() > 0.45 * SR:
                break
            y += formant_gain(fh, vowel) * np.sin(h * phase) / h ** 1.2
        y += 0.01 * rng.standard_normal(n)       # breath
        env = np.minimum(1.0, t / 0.05) * np.minimum(1.0, (n - np.arange(n)) / (0.06 * SR))
        s0 = int(t_cursor * SR)
        e0 = min(total_n, s0 + n)
        out[s0:e0] += (y * env)[: e0 - s0]
        t_cursor += dur
    return out


def drums(total_n, beat_sec, start_beat_sec, n_beats, rng, pickup_beats=0):
    out = np.zeros(total_n)

    def put(x, at):
        s0 = int(at * SR)
        if s0 < 0 or s0 >= total_n:
            return
        e0 = min(total_n, s0 + len(x))
        out[s0:e0] += x[: e0 - s0]

    kn = int(0.3 * SR)
    t = np.arange(kn) / SR
    f = 45 + 75 * np.exp(-t / 0.03)
    kick = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.12)
    sn = int(0.2 * SR)
    ts = np.arange(sn) / SR
    bb, aa = signal.butter(2, [1000 / (SR / 2), 6000 / (SR / 2)], btype="band")
    hb, ha = signal.butter(2, 6000 / (SR / 2), btype="high")
    for b in range(-pickup_beats, n_beats):
        at = start_beat_sec + b * beat_sec
        for half in (0, 1):
            hn = int(0.05 * SR)
            hat = signal.lfilter(hb, ha, rng.standard_normal(hn)) * np.exp(-np.arange(hn) / (0.012 * SR))
            put(0.12 * hat, at + half * beat_sec / 2)
        if b < 0:
            continue
        if b % 4 in (0, 2):
            put(0.6 * kick, at)
        else:
            snare = signal.lfilter(bb, aa, rng.standard_normal(sn)) * np.exp(-ts / 0.06)
            snare += 0.4 * np.sin(2 * np.pi * 185 * ts) * np.exp(-ts / 0.04)
            put(0.35 * snare, at)
    return out


def reverb(x, wet, rng):
    """Small Schroeder reverb: four combs into two allpasses."""
    if wet <= 0:
        return x
    y = np.zeros_like(x)
    for d, g in ((1116, 0.78), (1188, 0.77), (1277, 0.76), (1356, 0.75)):
        d = d // 2
        a = np.zeros(d + 1)
        a[0], a[d] = 1.0, -g
        y += signal.lfilter([1.0], a, x)
    for d, g in ((556 // 2, 0.5), (441 // 2, 0.5)):
        b = np.zeros(d + 1)
        b[0], b[d] = -g, 1.0
        a = np.zeros(d + 1)
        a[0], a[d] = 1.0, -g
        y = signal.lfilter(b, a, y)
    return x + wet * y / 4.0


INSTRUMENTS = {"piano": piano, "epiano": epiano, "pad": pad, "guitar": guitar}


# ---- arranging ----------------------------------------------------------------------------------

def voice(ch: chordlib.Chord, center: int):
    """Close-position upper voicing (root, 3rd, 5th, 7th, 9th) whose mean lies nearest `center`,
    lowest note at or above MIDI 53."""
    pcs = [(ch.root + i) % 12 for i in chordlib.QUALITIES[ch.quality]]
    best = None
    for rot in range(len(pcs)):
        order = pcs[rot:] + pcs[:rot]
        notes = []
        for pc in order:
            low = notes[-1] + 1 if notes else 53
            m = low + ((pc - low) % 12)
            notes.append(m)
        for shift in (0, 12):
            cand = [x + shift for x in notes]
            cost = abs(np.mean(cand) - center)
            if cand[-1] <= 84 and (best is None or cost < best[0]):
                best = (cost, cand)
    return best[1]


def bass_note(pc: int, low: int):
    return low + ((pc - low) % 12)


def render(spec: dict):
    rng = np.random.default_rng(spec["seed"])
    bpm = spec["bpm"]
    beat = 60.0 / bpm
    db = spec["downbeat_sec"]
    cents = spec.get("cents", 0.0)
    prog = spec["chords"] * spec.get("repeat", 1)
    if "tail" in spec:
        prog = prog + spec["tail"]
    n_beats = sum(b for _, b in prog)
    total = db + n_beats * beat + 1.2
    total_n = int(total * SR)
    mix = np.zeros(total_n)

    def add(x, at, gain):
        s0 = int(round(at * SR))
        e0 = min(total_n, s0 + len(x))
        if e0 > s0:
            mix[s0:e0] += gain * x[: e0 - s0]

    inst = INSTRUMENTS[spec["instrument"]]
    pattern = spec.get("pattern", "half")
    center = spec.get("center", 63)
    bass_low = spec.get("bass_low", 36)
    pos = 0.0
    for sym, beats in prog:
        ch = chordlib.parse(sym)
        start = db + pos * beat
        up = spec.get("voicings", {}).get(sym) or voice(ch, center)
        if pattern == "sustain":
            hits = [(0.0, beats)]
        elif pattern == "half":
            hits = [(k, min(2.0, beats - k)) for k in np.arange(0, beats, 2.0)]
        elif pattern == "quarter":
            hits = [(k, 0.9) for k in np.arange(0, beats, 1.0)]
        elif pattern == "strum":   # D . D U . U D U
            hits = [(k, 0.45) for k in np.arange(0, beats, 0.5) if (k % 4) not in (0.5, 2.0)]
        else:
            raise ValueError(pattern)
        for k, d in hits:
            vel = 0.55 + 0.25 * rng.random()
            spread = 0.012 if spec["instrument"] == "guitar" else 0.004
            order = up if (k * 2) % 2 == 0 else list(reversed(up))
            for j, m in enumerate(order):
                add(inst(m, d * beat, vel, cents, rng), start + k * beat + j * spread * rng.uniform(0.6, 1.4), 0.22)
        if spec.get("bass", True):
            bp = bass_note(ch.bass, bass_low)
            step = 2.0 if spec.get("bass_pattern", "half") == "half" else 1.0
            bfn = {"synth": synth_bass, "drive": drive_bass}.get(spec.get("bass_timbre"), bass)
            bgain = spec.get("bass_gain", 0.45)
            for k in np.arange(0, beats, step):
                add(bfn(bp, min(step, beats - k) * beat * 0.95, 0.8, cents, rng), start + k * beat, bgain)
                if spec.get("bass_octave"):   # left-hand octave: the upper bass note's partials land in the chord register
                    add(inst(bp + 12, min(step, beats - k) * beat * 0.95, 0.7, cents, rng), start + k * beat, 0.22)
        pos += beats

    if spec.get("melody"):
        mel = spec["melody"] * spec.get("repeat", 1)
        lead = spec.get("melody_instrument")
        if lead is None:
            mix += 0.5 * sing(mel, beat, db, total_n, cents, rng)
        else:
            at = db
            for m, b in mel:
                if m is not None:
                    add(INSTRUMENTS[lead](m, b * beat * 0.9, 0.9, cents, rng), at, 0.35)
                at += b * beat
    if spec.get("drums"):
        mix += drums(total_n, beat, db, n_beats, rng, pickup_beats=int(round(db / beat)))
    mix = reverb(mix, spec.get("reverb", 0.0), rng)
    mix += 1e-4 * rng.standard_normal(total_n)
    mix *= 0.8 / np.max(np.abs(mix))
    return mix, prog


def labels(spec: dict, prog) -> dict:
    out, beat = [], 0.0
    for sym, b in prog:
        out.append({"beat": beat, "len": b, "symbol": sym})
        beat += b
    return {
        "audio": spec["name"] + ".wav",
        "bpm": spec["bpm"],
        "downbeat_sec": spec["downbeat_sec"],
        "key": spec["key"],
        "chords": out,
        "source": spec["source"],
    }


# ---- the set ------------------------------------------------------------------------------------
# Melodies are (midi | None, beats).

CLIPS = [
    {   # Task case: F minor loop with Ab/C (3rd in the bass) and a sus resolution.
        "name": "fm9_loop_epiano", "seed": 101, "bpm": 90, "downbeat_sec": 0.0, "key": "F minor",
        "source": "keys", "instrument": "epiano", "pattern": "half", "drums": True, "reverb": 0.2,
        "bass_timbre": "drive", "bass_gain": 0.6, "bass_low": 36,
        # The plugin's own voicer output for this loop (SimpleVoicer), plus the reharm test melody.
        "voicings": {"Fm9": [56, 63, 67, 77], "Dbmaj7": [61, 65, 68, 72], "Ab/C": [60, 63, 68],
                     "Ebsus4": [58, 63, 68], "Eb": [58, 63, 67]},
        "melody_instrument": "epiano",
        "melody": [(72, 2), (68, 1), (67, 1), (65, 2), (68, 2), (75, 2), (72, 2), (70, 2), (67, 2)],
        "chords": [("Fm9", 4), ("Dbmaj7", 4), ("Ab/C", 4), ("Ebsus4", 2), ("Eb", 2)], "repeat": 2,
    },
    {   # Same family of chords, but tonal centre Ab major (starts and ends on Ab).
        "name": "ab_major_guitar", "seed": 102, "bpm": 104, "downbeat_sec": 0.3, "key": "Ab major",
        "source": "guitar", "instrument": "guitar", "pattern": "strum", "drums": True, "bass_pattern": "quarter",
        "chords": [("Ab", 4), ("Db", 4), ("Eb", 4), ("Fm", 4), ("Ab", 4), ("Db", 4), ("Eb", 4), ("Ab", 4)],
    },
    {   # Inversions in C: C G/B Am F C/E F G C.
        "name": "c_inversions_piano", "seed": 103, "bpm": 96, "downbeat_sec": 0.0, "key": "C major",
        "source": "keys", "instrument": "piano", "pattern": "quarter", "center": 66,
        "chords": [("C", 4), ("G/B", 4), ("Am", 4), ("F", 4), ("C/E", 4), ("F", 4), ("G", 4), ("C", 4)],
    },
    {   # Slash chords with the 3rd in the bass, Ab major.
        "name": "ab_slash_piano", "seed": 104, "bpm": 80, "downbeat_sec": 0.2, "key": "Ab major",
        "source": "keys", "instrument": "piano", "pattern": "half", "bass_octave": True,
        "voicings": {"Ab/C": [63, 68, 72], "Eb/G": [63, 70, 75], "Db/F": [61, 68, 73]},
        "chords": [("Ab/C", 4), ("Db", 4), ("Eb/G", 4), ("Ab", 4), ("Fm", 4), ("Db/F", 4), ("Eb", 4), ("Ab", 4)],
    },
    {   # Minor key with a sung melody over a pad; Am F C G ... ends on Am.
        "name": "a_minor_vocal_pad", "seed": 105, "bpm": 84, "downbeat_sec": 0.0, "key": "A minor",
        "source": "vocal", "instrument": "pad", "pattern": "sustain", "drums": True, "reverb": 0.25,
        "chords": [("Am", 4), ("F", 4), ("C", 4), ("G", 4), ("Am", 4), ("F", 4), ("E7", 4), ("Am", 4)],
        "melody": [(76, 1), (72, 1), (71, 1), (69, 1), (72, 2), (69, 2), (67, 1.5), (64, 0.5), (67, 2),
                   (71, 2), (74, 2), (76, 1), (72, 1), (69, 2), (72, 2), (77, 2), (76, 1), (74, 1),
                   (71, 2), (68, 2), (69, 4)],
    },
    {   # Harmonic-minor cadence V7-i in E minor, plucked guitar.
        "name": "e_minor_guitar", "seed": 106, "bpm": 112, "downbeat_sec": 0.4, "key": "E minor",
        "source": "guitar", "instrument": "guitar", "pattern": "strum", "drums": True,
        "chords": [("Em", 4), ("Am", 4), ("B7", 4), ("Em", 4), ("C", 4), ("Am", 4), ("B7", 4), ("Em", 4)],
    },
    {   # Sevenths: ii-V-I flavour in D major on electric piano.
        "name": "d_major_sevenths_epiano", "seed": 107, "bpm": 88, "downbeat_sec": 0.0, "key": "D major",
        "source": "keys", "instrument": "epiano", "pattern": "half", "center": 64,
        "chords": [("Dmaj7", 4), ("Bm7", 4), ("Em7", 4), ("A7", 4), ("Dmaj7", 4), ("Gmaj7", 4), ("Em7", 4), ("A7", 4)],
        "tail": [("Dmaj7", 4)],
    },
    {   # Detuned +46 cents (old tape, nearly a quarter tone), Bb major with slash chords.
        "name": "bb_detuned_piano", "seed": 108, "bpm": 100, "downbeat_sec": 0.0, "key": "Bb major",
        "source": "keys", "instrument": "piano", "pattern": "quarter", "cents": 46.0,
        "chords": [("Bb", 4), ("Eb/G", 4), ("F/A", 4), ("Bb", 4), ("Gm", 4), ("Eb", 4), ("F", 4), ("Bb", 4)],
    },
    {   # Detuned -42 cents, G major pop with a sung melody.
        "name": "g_major_vocal_flat", "seed": 109, "bpm": 120, "downbeat_sec": 0.25, "key": "G major",
        "source": "vocal", "instrument": "piano", "pattern": "half", "cents": -42.0, "drums": True,
        "chords": [("G", 4), ("Em", 4), ("C", 4), ("D", 4)], "repeat": 2,
        "melody": [(71, 1), (74, 1), (79, 1), (74, 1), (76, 2), (71, 2), (72, 1), (76, 1), (79, 2),
                   (78, 1), (74, 1), (69, 2)],
    },
    {   # Aeolian bVII-i cadences: Dm C Bb C, ends Dm.
        "name": "d_minor_bvii_pad", "seed": 110, "bpm": 76, "downbeat_sec": 0.0, "key": "D minor",
        "source": "loop", "instrument": "pad", "pattern": "half", "drums": False, "reverb": 0.3,
        "chords": [("Dm", 4), ("C", 4), ("Bb", 4), ("C", 4), ("Dm", 4), ("Bb", 4), ("C", 4), ("Dm", 4)],
    },
    {   # F major with inversions on electric piano (F/A, C/E, Bb/D).
        "name": "f_major_slash_epiano", "seed": 111, "bpm": 92, "downbeat_sec": 0.15, "key": "F major",
        "source": "keys", "instrument": "epiano", "pattern": "quarter", "drums": True,
        "chords": [("F", 4), ("C/E", 4), ("Dm", 4), ("Bb/D", 4), ("F/A", 4), ("Bb", 4), ("C7", 4), ("F", 4)],
    },
    {   # Relative ambiguity the other way: C major vi-IV-I-V that resolves to C (V-I).
        "name": "c_major_vi_start_guitar", "seed": 112, "bpm": 128, "downbeat_sec": 0.0, "key": "C major",
        "source": "guitar", "instrument": "guitar", "pattern": "strum", "drums": True,
        "chords": [("C", 4), ("Am", 4), ("F", 4), ("G", 4), ("Am", 4), ("F", 4), ("G", 4), ("C", 4)],
    },
    {   # Guitar-only slash chords: the low string is the bass and its bright partials (a 12th up)
        # fake a fifth, so Ab/C competes with Cm. Cm itself appears too, for contrast.
        "name": "eb_slash_guitar", "seed": 114, "bpm": 98, "downbeat_sec": 0.0, "key": "Eb major",
        "source": "guitar", "instrument": "guitar", "pattern": "strum", "bass": False, "reverb": 0.15,
        "voicings": {"Eb": [51, 58, 63, 67, 70], "Ab/C": [48, 56, 60, 63, 68], "Bb/D": [50, 58, 62, 65],
                     "Cm": [48, 55, 60, 63, 67], "Bb": [46, 53, 58, 62, 65], "Ab": [44, 51, 56, 60, 63]},
        "chords": [("Eb", 4), ("Ab/C", 4), ("Bb/D", 4), ("Eb", 4), ("Cm", 4), ("Ab/C", 4), ("Bb", 4), ("Eb", 4)],
    },
    {   # Sus chords and a dominant 7 sus in A major, 2 chords per bar.
        "name": "a_major_sus_piano", "seed": 113, "bpm": 72, "downbeat_sec": 0.0, "key": "A major",
        "source": "keys", "instrument": "piano", "pattern": "half",
        "chords": [("A", 2), ("Asus4", 2), ("D/F#", 2), ("D", 2), ("E7sus4", 2), ("E7", 2), ("A", 4),
                   ("F#m", 2), ("C#m/E", 2), ("D", 2), ("E", 2), ("A", 4)],
    },
]


def write_wav(path, x):
    pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "tests", "fixtures", "eval"))
    ap.add_argument("--only", default=None, help="comma-separated clip names")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None
    for spec in CLIPS:
        if only and spec["name"] not in only:
            continue
        x, prog = render(spec)
        for sym, _ in prog:
            chordlib.parse(sym)
        write_wav(os.path.join(args.out, spec["name"] + ".wav"), x)
        with open(os.path.join(args.out, spec["name"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(labels(spec, prog), f, indent=2)
            f.write("\n")
        print(f"{spec['name']}: {len(x) / SR:.1f} s, key {spec['key']}")


if __name__ == "__main__":
    main()
