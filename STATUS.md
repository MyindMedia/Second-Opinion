# Status: session 3 (GitHub, Mac and Windows builds, persistence, accuracy)

Date: October 26, 2026. Code lives at https://github.com/MyindMedia/Second-Opinion (private).

## CI on GitHub (measured, commit fe4eaca)

| Job | Result |
|---|---|
| macOS (universal) | build Debug + Release, ctest, pluginval strictness 5, auval: pass |
| Windows (x64, VS 2022) | build Debug + Release, ctest 154/154, pluginval strictness 5: pass |
| Linux (dev check) | build, ctest, pluginval: pass |
| UI | typecheck + Vitest: pass |
| Eval (chord accuracy gate) | pass |

## Added in session 3

- **Session save and recall** (FR-ST-01, ADR 0011): analysis, corrections, locks, generation
  history, favorites and the built song come back with the project, with no re-analysis. Captured
  audio is cached locally as FLAC by hash, never stored in the project. New native functions
  `setSettings`, `clearCache`, `restoreSnapshot` (undo and redo across chords, locks, suggestions
  and song, 64 steps).
- **Accuracy** (P2-05, ADR 0005): 14-clip labelled eval set, `so_analyze_cli`, `tools/eval`, CI gate.
  Before and after: majmin 95.6 to 99.4, sevenths 82.1 to 88.2 (99.4 with Basic Pitch), bass 96.3
  to 99.9, key 13/14 to 14/14. Fixed: A♭/C read as Cm, relative major/minor key choice, global
  tuning offset, downbeat phase.
- **Release engineering**: nightly workflow (pluginval 10, sanitizers), release workflow on `v*`
  tags, macOS .pkg and Windows Inno Setup scaffolding with optional signing (docs/RELEASING.md),
  opt-in ONNX minimal build path (`SO_ORT_MINIMAL`, not yet built).
- **Portability fixes found by the first Mac and Windows builds**: a namespace alias clang rejects,
  missing standard includes for MSVC, npm called through `cmd /c` in the Visual Studio build, ASCII
  test names, locale-independent number parsing, pluginval exit code on Windows.
- Git LFS dropped: the model and test WAVs (about 14 MB) are ordinary git files.

## Open decisions for Lawrence

- ONNX Runtime 1.21 for macOS needs macOS 13.3+, while the plugin targets 12.0 (Q9).
- Plugin codes and manufacturer name before any public build (Q1); see docs/OPEN_QUESTIONS.md.

---

# Session 2 notes (listening, reharm, song builder)

Date: October 26, 2026. Built and tested on a Linux cloud machine (2 cores). macOS and Windows
builds are configured (presets and CI) but were not compiled in this session; the first Mac build
happens on Lawrence's machine (see README). Session 1 notes: docs/STATUS-session1.md.

## What the plugin does now (real, not simulated)

1. **Listens.** Capture from the DAW track while the transport plays (bar-aligned to the host PPQ),
   open an audio file with the native chooser, or drop one on the window (decoded in the page).
   Tempo and downbeat are detected for files; the host tempo wins when the audio agrees.
2. **Reads the harmony.** Basic Pitch (ONNX) transcribes notes; DSP chroma + HMM decoding finds
   chords with confidence, top-3 alternatives and slash chords; key and Roman numerals follow.
3. **Suggests reharmonizations.** 6 to 12 suggestions from all six technique families, with a
   voice-leading solver, bass motion, tension and smoothness controls, locks, and plain-language
   explanations.
4. **Builds out the song** (Build out mode). Type a description and pick moods; the offline mood
   engine (545 phrases) plans Intro, Verse, Pre-Chorus, Chorus, Bridge and Outro around the idea,
   with three options per section and transitions that lead into the next section. Optional online
   AI (opt-in, text only) can read the description instead (ADR 0010).
5. **Plays and exports.** Preview synth (keys, pad, bass) auditions the original, a suggestion, a
   section or the whole song; MIDI drags into the DAW or exports to a folder.

## Agents and merges

| Work | Branch | Result |
|---|---|---|
| Listening engine (P2-01..P2-03) | agent/analysis | 302 assertions; 16/16 beats on synthetic progressions; 64 s analysed in about 1.8 s |
| Reharm engine (P3-01, P3-02, P4-01, P4-02, P4-04) | agent/reharm | 22 test cases; 68 to 83 ms for 12 suggestions over 32 bars |
| Song builder + mood engine (FR-SB, PRD 5.9) | agent/song | 53 test cases; ADR 0007 |
| Capture, files, preview synth (P1-06, P1-07, P3-04) | agent/audio-io | RT-safe; synth 1.3 to 2.4% CPU (optimised build) |
| ONNX Runtime + Basic Pitch (P1-08) | agent/ml | 44/44 notes match the Python reference, F-measure 1.000 |
| Build out UI + bridge contract | agent/ui | 31 tests; docs/bridge-contract.md; screenshots in docs/screenshots |
| Integration (bridge, SessionEngine, SongBridge, AiMoodClient, tempo) | main | this file |

## Test results (measured)

```
ctest:            100% tests passed, 0 tests failed out of 140
so_unit_tests:    All tests passed (355544 assertions in 122 test cases)
so_rt_tests:      All tests passed (32620 assertions in 11 test cases)
so_ml_tests:      All tests passed (558 assertions in 7 test cases)
UI (Vitest):      Tests 31 passed (31); tsc --noEmit clean
pluginval 1.0.4:  strictness 5, VST3 (integrated build): SUCCESS, exit code 0
```

## End-to-end test in the real plugin (Linux Standalone, measured)

Input: a synthesized 22 s keys + melody idea at 92 BPM (Fm9 | D♭maj7 | A♭/C | E♭sus4 E♭7, twice).

- Open audio file (native chooser), analysed in about 15 s on the shared VM including Basic Pitch.
- Tempo detected 92.0 BPM, downbeat 12 ms; 8 bars, chords on the bar lines.
- Heard: Fm9, D♭, Cm (should be A♭/C, flagged uncertain), E♭sus4, E♭7; key read as A♭ major (the
  relative of F minor; the loop is ambiguous and the key can be overridden).
- 12 real suggestions with explanations. Build out with "dark late-night R&B, hopeful chorus" plus
  Dark and Hopeful built a 61-bar song (Intro, Verse 1 = your idea, Pre-Chorus, Chorus 1, Verse 2,
  Pre-Chorus 2, Chorus 2, Bridge, Chorus 3, Outro) with three options per section.
- The native drag strip filled with the section's voiced lanes; Listen played the whole song.

## Bugs found by end-to-end testing and fixed

- Files analysed at a fixed 120 BPM when the host sends no tempo: grid misaligned. Added tempo and
  downbeat detection (TempoEstimator).
- Chord symbols with flats and sharps were mangled crossing from the WebView to C++ (Latin-1-decoded
  UTF-8), so the drag strip stayed empty in Build out. C++ now repairs incoming strings; the UI
  sends ASCII symbols. Regression test in tests/rt/bridge_json_test.cpp.
- A decaying tail made a 9th partial bar. Trailing tail bars are now trimmed.

## Known limits

- Accuracy numbers come from synthetic audio; a labelled set of real stems is still needed (Q4).
- Slash chords with the 3rd in the bass (A♭/C) can read as the bass triad (Cm), because overtones of
  the bass fake a fifth; they are flagged as uncertain for a 2-click correction.
- Relative major/minor key choice can go either way on ambiguous loops; use the key override.
- The macOS ONNX Runtime library is 67 MB (over the 60 MB budget) until the minimal static build.
- Online AI is a no-op on Linux (curl disabled); macOS and Windows use the native HTTP stack.
- MIDI drag into real DAWs (ADR 0002 host matrix) still needs testing on Lawrence's Mac.

## Next 3 tickets

1. First Mac build and DAW drag test (Logic, Live, FL and others; fill in ADR 0002).
2. Real-stem evaluation set and an accuracy gate in CI (P2-05), then tune slash/inversion scoring.
3. ONNX Runtime minimal static build to meet the binary-size budget (ADR 0009).
