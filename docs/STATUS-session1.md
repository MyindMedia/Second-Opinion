# Status: Phase 1, session 1

Date: October 26, 2026. Built and tested on a Linux cloud machine (2 cores). macOS and Windows builds
are configured (presets and CI) but were not compiled in this session; the first Mac build happens on
Lawrence's machine (see README).

## Tickets

| Ticket | State | Commit |
|---|---|---|
| P1-01 Scaffold (CMake, presets, CPM, JUCE 9.0.4, Catch2, pass-through plugin) | Done | 6fbf581, cdfcb59 |
| P1-02 CI (build, ctest, pluginval 5, auval, UI checks, artifacts) | Written, not yet run (no GitHub repo) | 0aa6f8f |
| P1-03 Theory core (Pitch, Interval, ChordQuality, Chord, parser, namer, Key, RomanNumeral) | Done | cc255eb |
| P1-04 WebView editor with bound parameters, resizable, offline | Done, and the UI goes beyond the stub: full main workspace design | 3ceab56, b838e6b |
| P1-05 Native MIDI drag strip | Implemented; DAW matrix pending (needs real hosts) | 3ceab56 |
| P1-06 Capture, P1-07 File drag-in, P1-08 ONNX Basic Pitch | Not started | |

## What is real and what is simulated

Real today:
- Pass-through audio processor, 0 samples latency, allocation-free `processBlock`.
- 15 automatable parameters, bound to the UI both ways (verified, see below).
- Chord parsing, naming (Pop/Jazz, slash preference, flat/sharp spelling), keys, Roman numerals.
- Selecting a suggestion sends it to C++, which re-parses it, voices it and makes the four MIDI lanes
  for the drag strip and Export MIDI.
- Session state: parameters, selected suggestion, UI settings and window size are saved with the
  project and restored.

Simulated with the example session (F minor, 92 BPM, 32 bars) until the engine tickets land:
- Capture, analysis progress, detected chords and confidences, and the eight suggestions.
- Generate and Shuffle filter and rank the example suggestions by Color and Tension.
- Audition moves the playhead but plays no sound yet (preview synth is P3-04).

## Test results (measured)

Unit tests, Linux Debug (`so_unit_tests`):
```
All tests passed (68233 assertions in 27 test cases)
```

Real-time and state tests (`so_rt_tests`):
```
All tests passed (6007 assertions in 3 test cases)
```
- Negative control: the allocation trap catches a deliberate allocation.
- 2000 blocks of 128 samples at 48 kHz: zero heap allocations, output bit-identical to input.
- State round-trip, plus 200 random garbage blobs ignored without crashing.

ctest, Linux Debug:
```
100% tests passed, 0 tests failed out of 30
```

UI (Vitest + typecheck):
```
Tests  6 passed (6)
tsc --noEmit: no errors
```

pluginval 1.0.4, strictness 5, VST3, Linux Debug, editor tests under a virtual display:
```
SUCCESS (exit code 0)
```

Clean preset build from scratch (`cmake --preset linux-release`, CPM downloading JUCE 9.0.4):
```
100% tests passed, 0 tests failed out of 30
pluginval strictness 5 on the Release VST3: SUCCESS (exit code 0)
```

## End-to-end checks in the real plugin (Linux Standalone, measured)

1. The WebView UI loads from BinaryData with bundled fonts and no network.
2. Dragging Tension to 68 in the UI and closing the app: the saved plugin state contains
   `tension = 68.0` (UI to parameter).
3. Writing `tension = 15.0` into the saved state and relaunching: the knob shows 15 (parameter to UI,
   the same path a host uses when restoring a project).
4. The selected suggestion (`s-dorian`) came back after relaunch without re-running the flow.
5. Picking a card filled all four native drag handles with lane previews computed in C++.

## Bugs found and fixed while testing

- The WebView2 backend was requested on every platform, which made macOS and Linux show the
  fallback message instead of the UI. Now Windows-only (ADR 0001).
- A `juce::String` built from a non-ASCII literal ("Export MIDI…") garbled the label. Now
  `String::fromUTF8`.
- Muted text token failed WCAG contrast; adjusted (ADR 0003).

## Numbers

| Item | Value | Measured or estimated |
|---|---|---|
| Embedded UI size (ui/dist, fonts included) | 472 KB | Measured |
| UI JavaScript / CSS | 84 KB / 39 KB (27 KB / 9 KB gzipped) | Measured |
| First configure (downloads JUCE) | about 70 s | Measured, cloud machine |
| Full Debug build | about 4 min on 2 cores | Measured, cloud machine |
| Analysis stage timings in the UI | 0.5 / 1.6 / 1.1 / 0.6 s | Simulated, not real |

## ADRs written

- 0001 WebView UI with a native drag strip
- 0002 MIDI drag to the DAW (host matrix to fill in)
- 0003 UI design direction and token changes
- 0004 Dependency versions

## Open questions for Lawrence

See `docs/OPEN_QUESTIONS.md`. Most urgent: plugin and manufacturer codes (Q1, they are baked into
saved projects), Apple Developer ID for signing (Q13), and which GitHub repo should run CI (Q15).

## Next 3 tickets

1. P1-05 finish: test the MIDI drag in Live, Logic, FL, Studio One, Reaper and Cubase on macOS and
   fill in ADR 0002.
2. P1-06 Capture: pre-allocated buffer, lock-free FIFO, worker drain, PPQ and tempo stamping, wired
   to the Capture button.
3. P1-07 File drag-in: decode WAV/AIFF/FLAC/MP3 on a worker, BPM prompt fallback.
