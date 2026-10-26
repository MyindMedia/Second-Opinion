#!/usr/bin/env python3
"""Chord and key evaluation for Second Opinion (PRD 8, ticket P2-05).

Runs so_analyze_cli on every labelled clip and scores the result mir_eval-style:

- majmin   root + triad (semitones below 8) for references that reduce to maj, min or N
- sevenths root + 12-semitone quality for references that reduce to maj, min, maj7, 7, min7 or N
           (9ths count as their 7th chord, added tones are dropped as in mir_eval)
- bass     sounding bass pitch class (slash note, or the root) over every chord frame
- root     root pitch class (diagnostic)
- key      exact key accuracy over clips, plus the MIREX weighted key score

Scores are duration weighted over all clips (10 ms frames on the reference timeline, in seconds,
so a detected grid that differs from the label grid is judged by what it actually says).
Uncovered reference time counts as "N" from the estimate.

  python3 tools/eval/eval_chords.py --cli build/dev/so_analyze_cli
  python3 tools/eval/eval_chords.py --cli ... --baseline tools/eval/baseline.json   # CI gate
  python3 tools/eval/eval_chords.py --cli ... --write-baseline tools/eval/baseline.json

The gate fails (exit 1) when any of majmin, sevenths, bass or key drops more than --tolerance
percentage points (default 2) below the baseline.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chordlib  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
FRAME = 0.01
GATED = ("majmin", "sevenths", "bass", "key")
SOURCES = {"vocal", "keys", "guitar", "loop"}


def validate_label(lab: dict, path: str):
    """Checks the PRD 8 label format; raises ValueError with the file name."""
    def need(cond, what):
        if not cond:
            raise ValueError(f"{path}: {what}")
    need(isinstance(lab.get("audio"), str) and lab["audio"], "'audio' must be a file name")
    need(isinstance(lab.get("bpm"), (int, float)) and lab["bpm"] > 0, "'bpm' must be > 0")
    need(isinstance(lab.get("downbeat_sec"), (int, float)) and lab["downbeat_sec"] >= 0, "'downbeat_sec' must be >= 0")
    need(isinstance(lab.get("key"), str), "'key' must be like 'F minor'")
    chordlib.parse_key(lab["key"])
    need(lab.get("source") in SOURCES, f"'source' must be one of {sorted(SOURCES)}")
    need(isinstance(lab.get("chords"), list) and lab["chords"], "'chords' must be a non-empty list")
    prev_end = None
    for c in lab["chords"]:
        need(isinstance(c, dict) and {"beat", "len", "symbol"} <= set(c), "each chord needs beat, len, symbol")
        need(c["len"] > 0, "chord len must be > 0")
        need(prev_end is None or c["beat"] >= prev_end - 1e-9, "chords must be sorted and not overlap")
        prev_end = c["beat"] + c["len"]
        chordlib.parse(c["symbol"])


def reference_intervals(lab: dict):
    spb = 60.0 / lab["bpm"]
    db = lab["downbeat_sec"]
    return [(db + c["beat"] * spb, db + (c["beat"] + c["len"]) * spb, chordlib.parse(c["symbol"])) for c in lab["chords"]]


def estimate_intervals(est: dict):
    out = []
    for s in est["segments"]:
        out.append((s["start_sec"], s["end_sec"], chordlib.Chord(s["root"], s["quality"], s["bass"])))
    return out


def chord_at(intervals, t):
    for a, b, c in intervals:
        if a <= t < b:
            return c
    return chordlib.N_CHORD


def score_clip(lab: dict, est: dict):
    ref = reference_intervals(lab)
    estv = estimate_intervals(est)
    sums = {k: [0.0, 0.0] for k in ("majmin", "sevenths", "bass", "root")}
    fns = {"majmin": chordlib.compare_majmin, "sevenths": chordlib.compare_sevenths,
           "bass": chordlib.compare_bass, "root": chordlib.compare_root}
    for a, b, rc in ref:
        n = max(1, int(round((b - a) / FRAME)))
        for i in range(n):
            t = a + (i + 0.5) * FRAME
            ec = chord_at(estv, t)
            for k, fn in fns.items():
                v = fn(rc, ec)
                if v is not None:
                    sums[k][0] += v * FRAME
                    sums[k][1] += FRAME
    return sums


def run_cli(cli: str, wav: str, lab: dict, args) -> dict:
    cmd = [cli, wav, "--quality", args.quality]
    if args.oracle_grid:
        cmd += ["--bpm", str(lab["bpm"]), "--downbeat", str(lab["downbeat_sec"])]
    elif not args.no_host_bpm:
        cmd += ["--host-bpm", str(lab["bpm"])]
    if args.use_source:
        cmd += ["--source", lab["source"]]
    if args.basic_pitch:
        cmd += ["--basic-pitch", args.basic_pitch]
    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if res.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed: {res.stderr.strip()}")
    return json.loads(res.stdout)


def main() -> int:
    ap = argparse.ArgumentParser(description="Chord/key evaluation (P2-05)")
    ap.add_argument("--cli", default=os.path.join(ROOT, "build", "dev", "so_analyze_cli"))
    ap.add_argument("--labels", default=os.path.join(ROOT, "tests", "fixtures", "eval"),
                    help="folder of label JSONs (audio paths are relative to each label)")
    ap.add_argument("--quality", default="balanced", choices=("fast", "balanced", "accurate"))
    ap.add_argument("--oracle-grid", action="store_true", help="pass the labelled bpm and downbeat instead of detecting them")
    ap.add_argument("--no-host-bpm", action="store_true", help="do not pass the labelled bpm as the host tempo")
    ap.add_argument("--use-source", action="store_true", help="pass the label's source as the source hint")
    ap.add_argument("--basic-pitch", default=None, help="path to nmp.onnx (CLI must be built with ONNX)")
    ap.add_argument("--baseline", default=None, help="baseline JSON to gate against")
    ap.add_argument("--tolerance", type=float, default=2.0, help="allowed drop in percentage points")
    ap.add_argument("--write-baseline", default=None)
    ap.add_argument("--json-out", default=None, help="write the full report as JSON")
    ap.add_argument("-v", "--verbose", action="store_true", help="print the estimated chords per clip")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.labels, "*.json")))
    if not files:
        print(f"no labels in {args.labels}", file=sys.stderr)
        return 2

    totals = {k: [0.0, 0.0] for k in ("majmin", "sevenths", "bass", "root")}
    key_exact, key_mirex, clips = 0, 0.0, []
    print(f"{'clip':28} {'majmin':>7} {'7ths':>7} {'bass':>7} {'root':>7}  key (ref -> est)")
    for path in files:
        with open(path, encoding="utf-8") as f:
            lab = json.load(f)
        validate_label(lab, path)
        wav = os.path.join(os.path.dirname(path), lab["audio"])
        est = run_cli(args.cli, wav, lab, args)
        sums = score_clip(lab, est)
        ks = chordlib.key_score(lab["key"], est["key"])
        key_exact += 1 if ks == 1.0 else 0
        key_mirex += ks
        row = {k: (100.0 * v[0] / v[1] if v[1] > 0 else None) for k, v in sums.items()}
        for k, v in sums.items():
            totals[k][0] += v[0]
            totals[k][1] += v[1]
        name = os.path.splitext(os.path.basename(path))[0]
        fmt = lambda v: f"{v:7.1f}" if v is not None else "      -"  # noqa: E731
        print(f"{name:28} {fmt(row['majmin'])} {fmt(row['sevenths'])} {fmt(row['bass'])} {fmt(row['root'])}"
              f"  {lab['key']} -> {est['key']}{'' if ks == 1.0 else '  X'}"
              f"  (grid {est['bpm']:.1f} BPM @ {est['downbeat_sec']:.2f} s, tuning {est.get('tuning_cents', 0):+.0f} c)")
        if args.verbose:
            print("    ref: " + " ".join(f"{c['symbol']}" for c in lab["chords"]))
            print("    est: " + " ".join(f"{s['symbol']}@{s['start_beat']:g}" for s in est["segments"]))
        clips.append({"clip": name, **row, "key_ref": lab["key"], "key_est": est["key"], "key_score": ks})

    metrics = {k: round(100.0 * v[0] / v[1], 2) if v[1] > 0 else 0.0 for k, v in totals.items()}
    metrics["key"] = round(100.0 * key_exact / len(files), 2)
    metrics["key_mirex"] = round(100.0 * key_mirex / len(files), 2)
    print(f"\nTOTAL ({len(files)} clips, {totals['bass'][1]:.0f} s of chords): "
          + ", ".join(f"{k} {metrics[k]:.1f}" for k in ("majmin", "sevenths", "bass", "root", "key", "key_mirex")))

    report = {"metrics": metrics, "clips": clips, "settings": {
        "quality": args.quality, "oracle_grid": args.oracle_grid, "host_bpm": not args.no_host_bpm,
        "use_source": args.use_source, "basic_pitch": bool(args.basic_pitch)}}
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
    if args.write_baseline:
        with open(args.write_baseline, "w", encoding="utf-8") as f:
            json.dump({"metrics": metrics, "settings": report["settings"], "tolerance_points": args.tolerance}, f, indent=2)
            f.write("\n")
        print(f"baseline written to {args.write_baseline}")

    if args.baseline:
        with open(args.baseline, encoding="utf-8") as f:
            base = json.load(f)["metrics"]
        failed = []
        for k in GATED:
            if metrics[k] < base[k] - args.tolerance:
                failed.append(f"{k} {metrics[k]:.2f} < baseline {base[k]:.2f} - {args.tolerance:g}")
        if failed:
            print("ACCURACY GATE FAILED: " + "; ".join(failed))
            return 1
        print(f"accuracy gate passed (tolerance {args.tolerance:g} points)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
