# ADR 0008: Audio I/O (capture, file loading, preview synth)

Status: accepted (P1-06, P1-07, P3-04)

## Context

The plugin must record the host track aligned to bars (FR-IN-01), load dropped files (FR-IN-02)
and audition suggestions through a light built-in synth (FR-OUT-03, FR-RH-09), without ever
allocating, locking or doing I/O on the audio thread (NFR-05) and within NFR-04's CPU budget.

## Decisions

### Capture (P1-06)

- **Headless pieces in `src/core/audio/`.** `SpscAudioFifo` (own planar SPSC ring with monotonic
  64-bit indices, no spare slot) and `CaptureMath` (bar alignment, length limits, mono downmix) have
  no JUCE and are unit-tested in `so_unit_tests`.
- **Buffers.** `prepareToPlay` sizes the FIFO to 0.5 s (at least 4 blocks) and the `CaptureBuffer`
  to 64 bars at the last known tempo, capped at 3 minutes (about 49 MB stereo at 48 kHz and
  120 BPM). `armCapture()` grows the buffer on the message thread if the current tempo needs more,
  so a slow song is not cut short by a stale tempo. Nothing is allocated once recording runs.
- **State machine.** One `std::atomic<int>`: Idle, Armed, Recording, Finishing, Done. The audio
  thread moves Armed to Recording (stamping PPQ, last bar start, tempo, time signature and sample
  time into atomics first) and Recording to Finishing (transport stop or length limit). The message
  thread moves Armed to Idle or Recording to Finishing (`stopCapture`). Only the worker leaves
  Finishing. The public `captureState()` folds Finishing into Recording.
- **Stop race.** The audio thread raises `audioInCapture` (seq_cst) around its state check and push.
  When the worker sees Finishing it waits for that flag to drop, then drains once more, so a block
  that was mid-push when Stop was pressed is never lost or half-copied.
- **Worker.** A dedicated `juce::Thread` drains every 10 ms while capturing (50 ms when idle). A
  message-thread timer was rejected: a modal dialog or slow UI work longer than the 0.5 s FIFO would
  drop audio. `drainCapture()` is public so tests can drive it.
- **Bar alignment.** Grid beats are quarter notes (`bpm` = host quarter-note tempo). Beat 0 is the
  first host bar line at or after the first captured sample; a start within one sample after a bar
  line keeps that bar (downbeat a fraction of a sample below 0). Bar lines come from the host's
  `ppqPositionOfLastBarStart` when given (meter changes), else from PPQ 0. `beatsPerBar` is
  `round(numerator * 4 / denominator)` (6/8 gives 3). Tempo is taken from the first block;
  tempo changes during a capture are not followed (MVP).
- **No tempo (AC5).** Without a playhead, recording starts as soon as it is armed and stops on Stop
  or the 3 minute cap. With a playhead but no tempo or PPQ, it starts on transport play. Either way
  `needsTempo()` is set, the grid uses 120 BPM with the downbeat at the capture start, and the UI
  asks for the BPM.
- **Output.** `takeCapturedClip()` returns `AudioClip` (mono average of the input channels) and
  `BeatGrid`; the capture's bar-1 PPQ becomes the audition origin.
- **FIFO overflow** (worker starved for 0.5 s) drops samples and sets `captureOverflowed()`; the UI
  should offer to recapture.

### File loading (P1-07)

`src/plugin/AudioFileLoader` uses `juce::AudioFormatManager::registerBasicFormats()` on a
one-thread `juce::ThreadPool`. Decoding runs in 32k-frame chunks with a cancel flag checked between
chunks and an atomic progress value; the result is posted with `MessageManager::callAsync` guarded by
an alive flag. `load()` only queues a job, so the message thread is never blocked (measured well
under the 16 ms budget in the test). Limit: 64 bars at the known BPM, capped at 3 minutes; longer
files are truncated and flagged. `clipFromPcm()` wraps PCM decoded in the WebView (float32 mono)
with the same limit and replaces non-finite samples with 0. Resampling stays in analysis.

### Preview synth (P3-04)

- **Headless DSP in `src/core/preview/`.** Keys = 2-operator FM electric piano (1:1 ratio,
  velocity sets modulation depth and a one-pole lowpass), pad = three detuned polyBLEP saws with a
  0.35 s attack into a two-pole lowpass, bass = sine plus saw into a two-pole lowpass at 700 Hz. 16
  voices shared, stealing the oldest releasing voice first. The sine is a branch-free parabolic
  approximation (no tables, no `std::sin` per sample).
- **Sample accuracy.** `render()` splits each block at note-on and note-off boundaries. A jump in
  the timeline (seek, loop wrap) or a new snapshot fast-releases held notes (about 15 ms) and
  restarts at the new position, including notes already sounding there.
- **Snapshot handoff.** `SnapshotExchange<T>`: the message thread owns every published snapshot via
  `shared_ptr`; the audio thread reads a raw pointer protected by one hazard pointer
  (acquire/release per block). Retired snapshots are freed on the message thread only when neither
  active nor announced. `std::atomic<std::shared_ptr>` was rejected: libstdc++ implements it with an
  internal lock, and the audio thread could drop the last reference.
- **Mixing.** Only while auditioning (plus a 50 ms gain fade-out after stop) the synth is added to
  the output at `previewLevel` dB (-60 dB = silent) with `juce::SmoothedValue`. Otherwise the
  preview path returns before touching the buffer: output equals input bit for bit (tested).
- **Sync.** Host playing with PPQ and tempo: beat = PPQ minus the audition origin (the capture's
  bar 1), no looping. Otherwise free-run from beat 0 at the snapshot BPM, looping over
  `lengthBeats`. `auditionBeat()` exposes the position for the UI playhead.
- **State.** The preview sound (Keys, Pad, Off) is stored as the session property `previewSound`.
  No parameter IDs changed.

## Measurements (shared 2-core VM, other builds running, so treat as upper bounds)

| Case | Debug build | -O2 (core synth only) |
|---|---|---|
| Synth, 16 keys voices, 48 kHz / 128 | 4.7% | 2.4% |
| Synth, 16 pad voices | 4.6% | 1.3% |
| Synth, 12 keys + 4 bass | | 2.4% |
| `processBlock` auditioning keys, 12 + 4 voices | 5.8% | not measured |
| `processBlock` capturing | 0.11% | not measured |

Optimised builds meet NFR-04 (< 3% auditioning at full 16-voice polyphony, < 1% capturing); the
Debug figures are over budget as expected for unoptimised code. Re-measure on target hardware in
P6-02.

## Consequences and open points

- NFR-08 (< 120 MB idle): the capture buffer is held from `prepareToPlay` (about 49 MB at
  48 kHz / 120 BPM, up to 69 MB at the 3 minute cap, double at 96 kHz). If profiling in P6-02 shows
  this matters with many instances, allocate on `armCapture()` instead and free after
  `takeCapturedClip()`.
- Each instance owns one drain thread that sleeps 50 ms between checks when idle.
- The bridge (`WebBridge.cpp`) is wired by the integrator using the API documented at the top of
  `PluginProcessor.h`.
