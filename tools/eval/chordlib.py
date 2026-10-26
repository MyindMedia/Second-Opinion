"""Chord symbol parsing and mir_eval-style reductions shared by the eval tools.

Covers the Second Opinion vocabulary (PRD 7.4): maj, m, dim, aug, sus2, sus4, 6, m6, 7, maj7,
m7, m7b5, dim7, add9, 9, maj9, m9, 7sus4, each with an optional slash bass, plus "N" (no chord).
"""

from __future__ import annotations

import re
from dataclasses import dataclass

NOTE_PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

# Intervals above the root (semitones, 14 = the 9th).
QUALITIES = {
    "maj": (0, 4, 7),
    "min": (0, 3, 7),
    "dim": (0, 3, 6),
    "aug": (0, 4, 8),
    "sus2": (0, 2, 7),
    "sus4": (0, 5, 7),
    "6": (0, 4, 7, 9),
    "min6": (0, 3, 7, 9),
    "7": (0, 4, 7, 10),
    "maj7": (0, 4, 7, 11),
    "min7": (0, 3, 7, 10),
    "hdim7": (0, 3, 6, 10),
    "dim7": (0, 3, 6, 9),
    "add9": (0, 4, 7, 14),
    "9": (0, 4, 7, 10, 14),
    "maj9": (0, 4, 7, 11, 14),
    "min9": (0, 3, 7, 10, 14),
    "7sus4": (0, 5, 7, 10),
}

# Symbol suffix -> quality name. Longest suffixes are tried first.
SUFFIXES = {
    "": "maj", "maj": "maj", "M": "maj",
    "m": "min", "min": "min", "-": "min",
    "dim": "dim", "o": "dim",
    "aug": "aug", "+": "aug",
    "sus2": "sus2", "sus4": "sus4", "sus": "sus4",
    "6": "6", "m6": "min6", "min6": "min6",
    "7": "7", "maj7": "maj7", "M7": "maj7",
    "m7": "min7", "min7": "min7",
    "m7b5": "hdim7", "dim7": "dim7", "o7": "dim7",
    "add9": "add9", "9": "9", "maj9": "maj9", "m9": "min9", "min9": "min9",
    "7sus4": "7sus4",
}

_SYMBOL = re.compile(r"^([A-G])([b#]*)(.*?)(?:/([A-G])([b#]*))?$")


@dataclass(frozen=True)
class Chord:
    root: int          # pitch class, -1 for N
    quality: str       # key of QUALITIES, or "N"
    bass: int          # absolute pitch class of the lowest note, -1 for N

    @property
    def is_n(self) -> bool:
        return self.root < 0

    def core(self) -> frozenset:
        """Interval set folded into one octave, with extensions above the 7th (the 9) dropped,
        as mir_eval does for 9th chords and for added tones when comparing triads/sevenths."""
        if self.is_n:
            return frozenset()
        return frozenset(i for i in QUALITIES[self.quality] if i < 12)

    def pcs(self) -> list:
        if self.is_n:
            return []
        return sorted({(self.root + i) % 12 for i in QUALITIES[self.quality]})


N_CHORD = Chord(-1, "N", -1)


def pitch_class(letter: str, accidentals: str) -> int:
    return (NOTE_PC[letter] + accidentals.count("#") - accidentals.count("b")) % 12


def parse(symbol: str) -> Chord:
    s = symbol.strip()
    if s in ("N", "NC", "N.C.", ""):
        return N_CHORD
    m = _SYMBOL.match(s)
    if not m:
        raise ValueError(f"unparseable chord symbol: {symbol!r}")
    root = pitch_class(m.group(1), m.group(2))
    suffix = m.group(3)
    if suffix not in SUFFIXES:
        raise ValueError(f"unknown chord quality {suffix!r} in {symbol!r}")
    bass = pitch_class(m.group(4), m.group(5)) if m.group(4) else root
    return Chord(root, SUFFIXES[suffix], bass)


# ---- mir_eval-style reductions ------------------------------------------------------------------

MAJMIN = {frozenset((0, 4, 7)): "maj", frozenset((0, 3, 7)): "min"}
SEVENTHS = {
    frozenset((0, 4, 7)): "maj",
    frozenset((0, 3, 7)): "min",
    frozenset((0, 4, 7, 11)): "maj7",
    frozenset((0, 4, 7, 10)): "7",
    frozenset((0, 3, 7, 10)): "min7",
}


def triad(c: Chord) -> frozenset:
    """Intervals below the 8th semitone (mir_eval majmin compares bitmap[:8])."""
    return frozenset(i for i in c.core() if i < 8)


def compare_majmin(ref: Chord, est: Chord):
    """1/0 when the reference reduces to maj, min or N, else None (excluded)."""
    if ref.is_n:
        return 1.0 if est.is_n else 0.0
    if triad(ref) not in MAJMIN:
        return None
    if est.is_n:
        return 0.0
    return 1.0 if (est.root == ref.root and triad(est) == triad(ref)) else 0.0


def compare_sevenths(ref: Chord, est: Chord):
    if ref.is_n:
        return 1.0 if est.is_n else 0.0
    if ref.core() not in SEVENTHS:
        return None
    if est.is_n:
        return 0.0
    return 1.0 if (est.root == ref.root and est.core() == ref.core()) else 0.0


def compare_bass(ref: Chord, est: Chord):
    """Sounding bass pitch class (the slash note, or the root for root position)."""
    if ref.is_n:
        return None
    if est.is_n:
        return 0.0
    return 1.0 if est.bass == ref.bass else 0.0


def compare_root(ref: Chord, est: Chord):
    if ref.is_n:
        return None
    if est.is_n:
        return 0.0
    return 1.0 if est.root == ref.root else 0.0


# ---- keys ---------------------------------------------------------------------------------------

_KEY = re.compile(r"^([A-G])([b#]*)\s+(major|minor)$", re.IGNORECASE)


def parse_key(text: str):
    m = _KEY.match(text.strip())
    if not m:
        raise ValueError(f"unparseable key: {text!r}")
    return pitch_class(m.group(1), m.group(2)), m.group(3).lower()


def key_score(ref: str, est: str) -> float:
    """MIREX weighted key score: 1 same, 0.5 fifth, 0.3 relative, 0.2 parallel, else 0."""
    rt, rm = parse_key(ref)
    et, em = parse_key(est)
    if (rt, rm) == (et, em):
        return 1.0
    if rm == em and (et - rt) % 12 in (5, 7):
        return 0.5
    if rm == "major" and em == "minor" and (et - rt) % 12 == 9:
        return 0.3
    if rm == "minor" and em == "major" and (et - rt) % 12 == 3:
        return 0.3
    if rt == et:
        return 0.2
    return 0.0
