# Second Opinion: Product Requirements Document (PRD)

**Product:** Second Opinion, an audio-aware reharmonizer plugin (VST3 / AU; AAX later)
**Owner:** Lawrence Berment
**Doc version:** 1.0 (October 17, 2026)
**Audience:** Claude Code (the implementer), Lawrence, and any future collaborators
**Companion docs:** `CLAUDE_BUILD_PROMPT.md` (kickoff prompt) and `UI_DESIGN_BRIEF.md` (UI design prompt)
**Source research:** `../ai-plugin-market-research.md` (October 16, 2026)

> **Conventions.** Anything marked **(estimate)** is an engineering guess, not a measurement. "MUST" means required for the MVP, "SHOULD" means strongly wanted for the MVP, and "LATER" means post-MVP. Sections and IDs are stable so tickets can reference them (e.g., `FR-AN-03`).

---

## 1. Overview and goals

### 1.1 One-liner
Put Second Opinion on a vocal, topline, sample or bounce. It **hears the harmony that's really there**, shows how sure it is, and offers **producer-grade reharmonizations** with real voice-leading, inversions and moving bass. It explains *why* each one works, and you drag the result into your DAW as MIDI. Everything runs **offline on your computer**.

### 1.2 Goals (MVP)
1. **G1, a trustworthy reading.** Detect chords and harmonic rhythm from clean audio. Show per-chord confidence and top alternatives, keep slash chords and inversions, and let the user correct any chord in 2 clicks or fewer.
2. **G2, suggestions worth using.** Generate reharmonizations that sound like a skilled keys player: smooth voice-leading, intentional bass motion, and tension and color the user controls. Not block chords.
3. **G3, explain it.** Every suggestion has a short plain-language "why this works" (function, Roman numeral, technique used).
4. **G4, fits the workflow.** Lock what you like and regenerate the rest. Audition inside the plugin, then drag MIDI out (chords, bass and top line as separate lanes).
5. **G5, local and dependable.** No cloud, no account needed to run, no GPU needed. It must pass pluginval and open reliably in the major DAWs.

### 1.3 Non-goals (MVP)
- Full-mix stem separation (users solo the harmonic or melodic source instead). **LATER**
- Generating melodies, drums or audio.
- Real-time (live-input) harmonization. Analysis is offline.
- Non-Western tuning systems or microtonal harmony. **LATER / maybe**
- Cloud features, accounts or telemetry.

### 1.4 Success metrics (targets, all estimates)
- Chord accuracy on Lawrence's labelled clean-stem test set: **≥ 80% weighted chord-symbol recall** (mir_eval-style "majmin" + bass) at MVP.
- 32 bars (~64 s at 120 BPM) analyzed in **≤ 10 s** on an Apple M1 / recent Intel i7 laptop.
- Beta users keep at least one suggestion (drag MIDI out) in **≥ 50%** of analysis sessions.
- **0** pluginval failures at strictness 5 on macOS and Windows for every release build.

---

## 2. Target users

| Persona | Description | Main job-to-be-done |
|---|---|---|
| **P1: Working producer / topliner session** (primary) | Pro or semi-pro producers. Often not trained keyboardists; they write around vocals, samples and loops | "This vocal needs chords that feel expensive, fast, without killing the vibe" |
| **P2: Theory-literate producer / keys player** | Knows theory and wants fresh options and speed | "Show me five strong alternatives to my changes and voice them well" |
| **P3: Beatmaker / sample flipper** | Starts from samples | "What are these chords, and how could I flip the harmony under this sample?" |
| **P4: Learner** (secondary) | Wants to understand harmony | "Why does this progression work?" |

The design priority is P1 > P2 > P3 > P4. Defaults should suit P1, with depth available for P2.

---

## 3. The problem (evidence from the market report)

1. **Audio-aware tools exist, but their musical output is basic.** Sound On Sound on Mixed In Key Wingman: chord generation *"produces fairly simplistic results, presented as block chords,"* with no inversions or bass voice-leading, and bass parts *"of the one-note-per-bar variety"* ([SOS](https://www.soundonsound.com/reviews/mixedinkey-wingman)).
2. **Chord readings from audio aren't trusted.** Users report Melodyne and Scaler 3 returning *"a completely different set of chords"* on the same file, and *"Audio to MIDI is not a 100% accurate process and can be pretty terrible for some source material"* ([Scaler forum](https://forum.scalermusic.com/t/why-is-it-that-melodyne-reports-different-chords-than-scaler-3/22713)).
3. **Musical intent gets "corrected" away.** *"What I want is for G/B to always remain G/B, regardless of my Voice Grouping settings"*; users also want G/F shown as G/F, not "F6 (sus2 b5)" ([Scaler forum, Apr 2026](https://forum.scalermusic.com/t/make-slash-chords-more-convenient/24635)).
4. **People want help and understanding, not autopilot.** *"Most importantly, it's not writing the damn song for me"* ([Gearspace](https://gearspace.com/threads/chord-progression-generating-plugins-recommendations.1256041/page-4)); *"…help me better understand the theory behind what I've created"* ([Scaler forum](https://forum.scalermusic.com/t/scaler-3-user-feedback-suggestions/20730)).
5. **Bloated UX drives people away.** *"After 2 hours, I rolled back to Scaler 2"* (same Scaler thread).
6. **The best workflow is locked to one DAW.** Logic Pro 12's Chord ID is praised (*"impressed with how good the harmonic analysis is"*) but is Logic-only and *"feels like a beginning, not a terminus"* ([CDM](https://cdm.link/logic-pro-12-hands-on/)).
7. **Local and offline matters.** Samplab's cloud service ended after Sept 17, 2026 ([samplab.com](https://samplab.com/)). A Wingman user reported *"SIX MINUTES"* of processing and a 4 GB VRAM need (Reddit, via search snippet; see the report).

**Positioning:** "Not genAI. Your audio, your call, your computer." It's a second opinion from a great keys player, not a song generator.

---

## 4. Product scope: MVP vs. later (matches the 90-day plan)

| Area | MVP (≈ 90 days, estimate) | Later |
|---|---|---|
| Input | Capture from host track while playing (armed); drag-in WAV/AIFF/FLAC/MP3; 4/4; up to 32 bars (MUST) / 64 bars (SHOULD) | Other meters; longer songs; MIDI-in analysis; multi-track context |
| Sources | Clean vocal, topline, keys, guitar, simple loops | Full mixes via on-device stem separation |
| Analysis | Basic Pitch notes + chroma + bass → HMM chord decoding; confidence; top-3 alternatives; slash chords; key and Roman numerals | Trained chord CRNN (ONNX) v1; modulation detection; extended vocab (13ths, altered) |
| Reharm | Rule-based candidates + voice-leading solver + heuristic ranker; tension, color, bass motion, smoothness, range, melody-on-top; lock and regenerate per bar | Learned ranker (small transformer); "artist taste packs"; style presets |
| Explain | Template-based "why" text per suggestion and per chord | Richer explanations; learning mode |
| Output | Internal preview synth (simple keys + bass); drag MIDI (Chords / Bass / Top / All); export .mid to folder | MIDI out port on hosts that support it; Chord Track export formats |
| Formats | VST3 + AU + Standalone; macOS (Apple Silicon + Intel) and Windows 10/11 x64 | AAX (Avid program + PACE signing); CLAP; Linux |
| UI | Single-window WebView UI (see §8.7); resizable | Compact mode polish; theming |

---

## 5. Functional requirements (user stories + acceptance criteria)

IDs: `FR-<area>-<n>`. Areas: IN (input), AN (analysis), RH (reharm), EX (explain), OUT (output), ST (state), SET (settings), UX (general).

### 5.1 Input
**FR-IN-01 Capture from host (MUST).** *As a producer, I arm Capture and press play in my DAW, and the plugin records the track's audio aligned to bars.*
- AC1: A "Capture" button arms recording. Recording starts on the next host transport start (or immediately if already playing) and stops on transport stop, when the user presses Stop, or at the length limit.
- AC2: The capture stores host tempo, time signature and the PPQ position of the first sample (from `AudioPlayHead::getPosition()`), so bar lines line up with the DAW.
- AC3: Audio passes through unchanged while capturing (bit-identical in a null test).
- AC4: No allocation or locking on the audio thread (verified by the RT-safety checks in §9).
- AC5: When the host has no playhead or tempo, the user is asked for a BPM (default 120) and the downbeat is taken from the capture start.

**FR-IN-02 Drag-in audio file (MUST).** *As a producer, I drop a file onto the plugin to analyze it.*
- AC1: Accepts WAV, AIFF, FLAC and MP3 (whatever JUCE's `AudioFormatManager` supports on the platform), up to 64 bars or 3 minutes.
- AC2: Decoding runs on a background thread, and the UI stays responsive.
- AC3: BPM comes from the host if available. Otherwise the user enters it, or picks "detect" (LATER). The user can nudge the downbeat in ±1/16 steps.

**FR-IN-03 Source hint (SHOULD).** *I can tell it what the source is (Vocal/Lead, Keys, Guitar, Loop/Mixed).*
- AC: The hint adjusts analysis weights (e.g., Vocal/Lead puts melody first and infers harmony from implied notes). The default is "Auto."

**FR-IN-04 Key override (SHOULD).** *I can lock the key or mode when the detector is wrong.*
- AC: Changing the key re-runs only the decode, Roman-numeral and reharm stages, with no re-transcription.

### 5.2 Analysis
**FR-AN-01 Progress and cancel (MUST).**
- AC1: The stages Preparing → Transcribing notes → Reading harmony → Writing suggestions show progress (0–100%).
- AC2: Cancel stops work within 500 ms (estimate target) and returns to the previous state.
- AC3: Errors show a human message (e.g., "Couldn't find a steady pitch, so try soloing the vocal") and a retry.

**FR-AN-02 Chord timeline (MUST).**
- AC1: The timeline shows bars, beats and chord segments with symbols on the harmonic-rhythm grid (minimum resolution 1 beat; MVP chords change on beats).
- AC2: Each segment shows a confidence value from 0 to 1, rendered as shading plus a numeric tooltip, so meaning doesn't rely on color alone.
- AC3: The key and a Roman numeral row are shown under the chords.

**FR-AN-03 Alternatives and correction (MUST).**
- AC1: Clicking a chord shows the top 3 alternative readings with confidences and the notes in each.
- AC2: Picking an alternative or typing a symbol (e.g., `G/B`, `Am7`, `F#m7b5`, `Csus2`) replaces the reading. Downstream suggestions update.
- AC3: User corrections are flagged as "user-locked" and survive re-analysis of other bars.

**FR-AN-04 Slash chords and inversions preserved (MUST).**
- AC1: When the detected bass is not the root, the symbol is a slash chord (e.g., `G/B`).
- AC2: The voicing solver (FR-RH) never changes a locked or user-set bass note.
- AC3: Naming follows the user's preference (§5.7). Common slash readings like `G/F` are preferred over exotic names like `F6sus2♭5` by default.

**FR-AN-05 Merge/split segments (SHOULD).** Users can merge two neighboring segments or split one at a beat.

### 5.3 Reharmonization
**FR-RH-01 Generate suggestions (MUST).** *I get a set of reharmonizations that fit the original melody or harmony.*
- AC1: Generates 6–12 suggestion cards for the selected range (default: whole capture) within the latency budget (§6).
- AC2: Each suggestion respects melody notes. Strong-beat melody notes must be chord tones or allowed tensions, according to the current Color setting.
- AC3: Results are deterministic for the same input, settings and seed. "Shuffle" changes the seed.

**FR-RH-02 Tension (MUST).** A 0–100 knob. Low means diatonic, consonant, mostly triads and 7ths. High allows extensions, secondary dominants, tritone subs and chromatic approach chords.
- AC: Raising tension raises the average tension score (an internal metric) monotonically across generated sets. A unit test proves this on fixtures.

**FR-RH-03 Color (MUST).** A selector of allowed technique families: Diatonic, Borrowed (modal interchange), Secondary dominants, Tritone subs, Chromatic mediants, Neo-Riemannian (P/L/R). Multi-select.
- AC: Suggestions only use enabled families, and each card lists the families it used.

**FR-RH-04 Bass motion (MUST).** Root / Inversions / Walking-stepwise / Pedal.
- AC: The generated bass lane follows the selected style. Pedal holds a user-chosen note across N bars.

**FR-RH-05 Voice-leading smoothness (MUST).** 0–100. High minimizes total semitone movement between chords. Low allows wider leaps and register changes.
- AC: The total voice movement metric decreases as smoothness increases (unit-tested).

**FR-RH-06 Voicing range and density (MUST).** Low and high note limits (default C2–C6 for chords, E1–C3 for bass) and density of 3–6 notes.
- AC: No generated note falls outside the limits.

**FR-RH-07 Melody on top (SHOULD).** When on, the top voice of each chord equals the current melody note (or sits a 3rd or 6th below it if the user chooses).

**FR-RH-08 Lock and regenerate (MUST).** *I lock bars or chords I love and regenerate only the rest.*
- AC1: Lock works per segment and per range. Locked segments are byte-identical across regenerations.
- AC2: The voice-leading into and out of locked chords is still smooth (the solver treats them as fixed constraints).

**FR-RH-09 Audition and A/B (MUST).** Click a card to hear it in sync with the source (if captured audio is available), with an Original ↔ Suggestion A/B toggle.
- AC: Audition starts within 100 ms of the click (estimate target) and is sample-aligned to the source.

**FR-RH-10 Favorites and history (SHOULD).** Star cards. Keep the last 20 generations, browsable.

### 5.4 Explain
**FR-EX-01 "Why this works" panel (MUST).** For the selected suggestion or chord: function (tonic, predominant, dominant), Roman numeral relative to the key, technique (e.g., "tritone sub of V7 → resolves down a half step to I"), and the melody relationship ("melody C is the 9th of B♭maj9").
- AC: Text is generated from structured data (no LLM), at most 3 short sentences per chord, with plain language first and the theory term in parentheses.

**FR-EX-02 Theory depth toggle (SHOULD).** Simple ↔ Detailed (Detailed adds voice-leading notes such as "3rd of V becomes 7th of I").

### 5.5 Output
**FR-OUT-01 Drag MIDI to DAW (MUST).** Drag handles for **Chords**, **Bass**, **Top line** and **All (multitrack)**.
- AC1: Dragging creates a temp `.mid` file (SMF type 1, 960 PPQ, tempo and time signature included) and starts an OS-level file drag that the DAW accepts. Tested in Ableton Live, Logic Pro, FL Studio, Studio One, Reaper and Cubase.
- AC2: The MIDI starts at the capture's bar 1 so it lines up when dropped at the same position.

**FR-OUT-02 Export to folder (MUST).** "Export MIDI…" writes the same files to a chosen folder, named `SecondOpinion_<project>_<suggestion>_<lane>.mid`.

**FR-OUT-03 Preview synth (MUST).** A simple built-in keys/pad and bass voice (a small, CPU-light sampled or synthesized sound) for auditioning, mixed into the plugin output only while auditioning, with level control.
- AC: When not auditioning, the output equals the input.

### 5.6 State
**FR-ST-01 Save and recall (MUST).** The DAW project restores analysis results, corrections, locks, the selected suggestion, settings and favorites.
- AC1: Reopening the project restores the exact view **without re-running analysis**.
- AC2: The state blob is versioned (`stateVersion`), and older versions migrate forward. pluginval state tests pass.
- AC3: Captured audio is **not** stored in the project by default. It's stored in a local cache (`<user app data>/SecondOpinion/cache/<hash>.flac`) and referenced by hash. If it's missing, analysis results still load, and audition falls back to MIDI only.

**FR-ST-02 Undo/redo (SHOULD).** For corrections, locks and generation steps (≥ 50 steps).

### 5.7 Settings
**FR-SET-01 (MUST)** Chord naming: Pop (`Cmaj7`, `C/E`), Jazz (`CΔ7`, `C-7`), and options for "prefer slash chords" and "♭/♯ spelling: auto / flats / sharps."
**FR-SET-02 (MUST)** Preview sound choice and level; MIDI export options (PPQ, velocity style, lane naming).
**FR-SET-03 (SHOULD)** Analysis quality: Fast / Balanced / Accurate (trades hop size against speed).
**FR-SET-04 (MUST)** UI scale (75–200%) and remember window size.
**FR-SET-05 (MUST)** About / licenses page listing third-party licenses (JUCE, VST3 SDK MIT, ONNX Runtime, Basic Pitch Apache-2.0, fonts).

### 5.8 General UX
**FR-UX-01 (MUST)** Works fully offline. No network calls at all (verified by a test that blocks the network, plus a code audit).
**FR-UX-02 (MUST)** Keyboard shortcuts: Space (audition), ←/→ (prev/next card), L (lock), R (regenerate), Cmd/Ctrl+Z.
**FR-UX-03 (SHOULD)** Empty, loading, error and low-confidence states as specified in `UI_DESIGN_BRIEF.md`.

---

## 6. Non-functional requirements

| ID | Requirement | Target (estimates unless stated) |
|---|---|---|
| NFR-01 | Formats | VST3, AU (macOS), Standalone at MVP; AAX LATER; CLAP LATER |
| NFR-02 | Platforms | macOS 12+ universal binary (arm64 + x86_64); Windows 10/11 x64. **Min OS versions are an open question** |
| NFR-03 | Plugin latency | **0 samples reported**. The plugin passes audio through, and all ML is offline |
| NFR-04 | Audio-thread CPU | < 1% of one core at 48 kHz / 128 samples when idle or capturing; < 3% while auditioning the preview synth |
| NFR-05 | RT safety | No heap allocation, locks, file I/O, logging or ONNX calls on the audio thread. Capture uses a pre-allocated lock-free FIFO |
| NFR-06 | Analysis speed | 32 bars at 120 BPM in ≤ 10 s on M1 / i7-1260P-class CPUs (CPU only, no GPU required) |
| NFR-07 | Reharm speed | ≤ 300 ms per generation of 12 suggestions over 32 bars |
| NFR-08 | Memory | < 300 MB peak during analysis; < 120 MB idle with UI open |
| NFR-09 | Binary size | < 60 MB per format, including the ONNX Runtime minimal build and models |
| NFR-10 | Offline | No network access, no account, no telemetry. License activation (if any) must support offline activation (a known Scaler 3 complaint) |
| NFR-11 | Stability | pluginval strictness 5 passes in CI on every commit; strictness 10 nightly; `auval` passes on macOS |
| NFR-12 | Determinism | Same input + settings + seed → identical suggestions and MIDI bytes |
| NFR-13 | UI performance | 60 fps interactions; idle UI CPU < 2%; open in < 1 s |
| NFR-14 | Accessibility | Contrast ≥ 4.5:1 for text; confidence shown by more than color alone; full keyboard navigation of cards |
| NFR-15 | Multi-instance | 10 instances in a session without crashes. ONNX sessions are shared or lazily created per instance; models are memory-mapped or shared |
| NFR-16 | Privacy | No user audio leaves the machine. The cache can be cleared from Settings |

---

## 7. Technical architecture

### 7.1 Stack
- **Framework:** JUCE, CMake build. *Note:* the brief says JUCE 8, but **JUCE 9 was released on July 21, 2026 (9.0.1 on Aug 10, 2026)** with the same licensing terms as JUCE 8 ([JUCE forum](https://forum.juce.com/t/juce-9-is-available-now/69175), [9.0.1](https://forum.juce.com/t/juce-9-0-1-is-out/69269)). **Recommendation: JUCE 9.0.x**, which keeps the JUCE 8 WebView APIs and adds an npm TypeScript WebView interop package. If Lawrence prefers JUCE 8, every API used here exists in 8.x too. *(Open question Q2.)*
- **Licensing:** the JUCE 8/9 **Starter** licence is free for closed-source commercial use up to **$20,000** in revenue or funding over the prior 12 months. Indie is $40/mo or $800 perpetual (up to $300k), and Pro is $175/mo or $3,500 perpetual ([JUCE licence](https://juce.com/legal/juce-8-licence/)). The **VST 3 SDK is MIT-licensed since v3.8.0 (Oct 20, 2025)** ([Steinberg](https://steinbergmedia.github.io/vst3_dev_portal/pages/Versions/Version+3.8.0.html)). AAX needs Avid developer access and PACE signing (LATER).
- **Language:** C++20. Core libraries have no JUCE GUI dependency, so they can be tested headless.
- **ML runtime:** ONNX Runtime (CPU execution provider), using a **minimal / reduced-operator build** to shrink binary size (the approach used by [basicpitch.cpp](https://github.com/sevagh/basicpitch.cpp/)). Link it **statically**, or rename and namespace it, to avoid DLL clashes with other plugins that load their own `onnxruntime.dll` (a risk; see §13).
- **Transcription model:** Spotify **Basic Pitch** `saved_models/icassp_2022/nmp.onnx` (Apache-2.0). Inputs and outputs: `serving_default_input_2:0` → `StatefulPartitionedCall:1` (note), `:2` (onset), `:0` (contour). It expects 22,050 Hz mono audio ([basic-pitch](https://github.com/spotify/basic-pitch/blob/main/basic_pitch/inference.py), [README](https://github.com/spotify/basic-pitch/blob/main/README.md)). Keep the LICENSE and NOTICE.
- **Optional real-time-safe inference (LATER):** [anira](https://github.com/anira-project/anira) or [RTNeural](https://github.com/jatinchowdhury18/rtneural/). The MVP doesn't need real-time ML.
- **UI:** JUCE `WebBrowserComponent` with native integration (TypeScript frontend built with Vite, embedded via `juce_add_binary_data`, served through `withResourceProvider`). See §8.7 and the design brief.
- **Tests:** Catch2 v3 (unit), CTest, pluginval, `auval`, Vitest (UI).

### 7.2 Component diagram (text)
```
┌──────────────────────────── Plugin (AudioProcessor) ────────────────────────────┐
│ Audio thread: processBlock()                                                     │
│   • passthrough  • CaptureWriter → LockFreeFifo  • PreviewSynth.render(snapshot) │
├──────────────────────────────────────────────────────────────────────────────────┤
│ Worker thread(s): AnalysisService (juce::ThreadPool, 1 job at a time)            │
│   CaptureAssembler → Resampler(22.05k mono) → BasicPitchRunner(ONNX)             │
│   → NoteEvents + Salience → ChromaExtractor (+bass chroma)                       │
│   → BeatGrid (host tempo/PPQ) → ChordDecoder (HMM/Viterbi, vocab)                │
│   → KeyEstimator → RomanAnalyzer → AnalysisResult                                │
│   ReharmService: CandidateGenerator → VoiceLeadingSolver → Ranker → Explainer    │
├──────────────────────────────────────────────────────────────────────────────────┤
│ Message thread: PluginEditor → WebView UI  ⇄  WebBridge (native fns/events, JSON) │
│   + NativeDragStrip (JUCE component, external file drag)                         │
│ State: APVTS (automatable params) + ValueTree (session) → get/setStateInformation │
└──────────────────────────────────────────────────────────────────────────────────┘
```

### 7.3 Audio capture pipeline
1. `prepareToPlay`: pre-allocate a `CaptureBuffer` (max 64 bars at the current tempo, capped at 3 min × 2 ch × sampleRate floats; about 69 MB at 48 kHz, an estimate) and a lock-free FIFO (`juce::AbstractFifo`, ~0.5 s).
2. Audio thread: when armed and the transport is playing, copy the block into the FIFO and record the first-block PPQ and sample time (atomics).
3. A worker drains the FIFO into the `CaptureBuffer`. When capture ends, it posts `CaptureComplete`.
4. The cache writer encodes FLAC to the app-data cache (background) and stores the SHA-256 hash in the session state.
5. Drag-in follows the same path from `AudioFormatReader` on a background thread.

### 7.4 Analysis pipeline
| Stage | MVP method | Output |
|---|---|---|
| Resample | Downmix to mono; resample to 22,050 Hz (`juce::LagrangeInterpolator` or r8brain (MIT), **license to verify**) | float buffer |
| Notes | Basic Pitch ONNX on overlapping windows (as in the reference implementation); note events via onset/frame thresholds | `NoteEvent{pitch, start, end, velocity, confidence}` + pitch salience matrix |
| Chroma | 12-bin chroma from Basic Pitch salience (preferred), plus a separate **bass chroma** (MIDI < 52) | per-frame chroma, bass chroma |
| Beat grid | From host tempo/PPQ (capture) or user BPM (drag-in); frames aggregated per beat | per-beat features |
| Chord decode | Template scoring over vocabulary (maj, min, dim, aug, sus2, sus4, 6, 7, maj7, m7, m7♭5, dim7, add9, 9, maj9, m9) × 12 roots × bass note, then **HMM/Viterbi** with self-transition bias to set harmonic rhythm. Confidence = posterior; keep the top 3 per segment | `ChordSegment{startBeat, lengthBeats, symbol, bass, confidence, alts[3]}` |
| Key | Krumhansl-style profile correlation over the note histogram, with a mode option | `Key{tonic, mode, confidence}` |
| Roman numerals | Function labelling relative to the key (including borrowed and secondary) | per-segment RN + function |
| v1 (LATER) | Small CRNN chord model (ONNX), trained per §8; swaps in behind the `IChordRecognizer` interface | same structs |

### 7.5 Reharmonization engine
1. **Constraints:** melody notes per beat (from Basic Pitch on Vocal/Lead, or the top voice of the original), locked segments, key, range, density.
2. **CandidateGenerator:** for each segment, produce candidate chords from the enabled technique families (diatonic subs, relative major/minor, ii–V insertion, secondary dominants, tritone subs, backdoor ♭VII7, modal interchange, chromatic mediants, Neo-Riemannian P/L/R) and score their melody compatibility.
3. **VoiceLeadingSolver:** dynamic programming / beam search over voicings per segment (voicing = sorted MIDI notes within range and density). Cost = α·total semitone movement + β·parallel 5ths/8ves penalty + γ·range-centering + δ·spacing rules (no 2nds below C3, etc.) + ε·melody-on-top violation. Locked bass and slash notes are hard constraints. α–ε are tied to the Smoothness knob.
4. **Bass generator:** root, inversion, walking (approach tones on beat 4) or pedal, per FR-RH-04.
5. **Ranker (MVP heuristic):** weighted sum of melody fit, tension target match, voice-leading cost, novelty vs. the original, and diversity across the set (MMR-style). The interface `IRanker` allows a learned model LATER.
6. **Explainer:** builds structured `Explanation{function, roman, technique, melodyRelation, voiceLeadingNotes}` → template text.
7. **Determinism:** all randomness through a seeded `std::mt19937_64` passed in explicitly.

### 7.6 MIDI output and drag-to-DAW
- `MidiWriter` builds `juce::MidiFile` (type 1, 960 PPQ, tempo and time-signature meta events) per lane and for "All."
- Files go to `<temp>/SecondOpinion/drag/<uuid>/*.mid`. Old drags are cleaned up at startup.
- **External drag** uses `juce::DragAndDropContainer::performExternalDragDropOfFiles`, called from a **native JUCE component** (`NativeDragStrip`) docked below the WebView. Starting an OS file drag from inside a WebView is unreliable across platforms, so this is a **Phase 1 spike** (P1-05) to confirm in Live, Logic, FL, Studio One, Reaper and Cubase.

### 7.7 State save and recall
- `AudioProcessorValueTreeState` holds automatable/user params: tension, smoothness, bass motion, color mask, range low/high, density, melodyOnTop, previewLevel, seed.
- A `ValueTree` holds the session: `stateVersion`, the analysis result (segments, key, notes compressed as delta-encoded int16), user corrections, locks, generations history (last 20), favorites, selected suggestion, cache hash, UI size and settings.
- `getStateInformation` writes XML/binary with `stateVersion`, and `setStateInformation` migrates forward. It must be robust to garbage input (pluginval fuzzes this).

### 7.8 Threading model (rules)
| Thread | Allowed | Forbidden |
|---|---|---|
| Audio | read atomics, lock-free FIFO push, render preview from an immutable snapshot (`std::shared_ptr` swapped via atomic on message thread with deferred release) | alloc, locks, I/O, ONNX, logging, `std::function` capture-alloc |
| Message | UI, WebBridge, state, starting jobs, swapping snapshots | long work (> 5 ms) |
| Worker | decoding, resampling, ONNX inference, decoding chords, reharm, file writes | touching UI components directly (post results via `juce::MessageManager::callAsync` / `AsyncUpdater`) |

Cancellation uses a `std::atomic<bool>` checked between stages and between ONNX windows. Progress is an `std::atomic<float>` polled by the UI timer at 30 Hz.

### 7.9 WebBridge contract (UI ⇄ C++)
- Native functions (JS → C++): `startCapture()`, `stopCapture()`, `analyzeFile(path)`, `cancel()`, `setChord(segmentId, symbol)`, `lock(segmentIds, bool)`, `generate({seed?})`, `audition(suggestionId|original, bool)`, `setParam(id, value)`, `exportMidi(suggestionId, lanes, folder?)`, `getState()`.
- Events (C++ → JS): `progress {stage, pct}`, `analysisReady {AnalysisResult}`, `suggestionsReady {Suggestion[]}`, `paramChanged {id, value}`, `transport {playing, ppq}`, `error {code, message}`.
- Parameters bind through JUCE's WebView relay classes (`WebSliderRelay` / `WebToggleButtonRelay` / `WebComboBoxRelay` with matching parameter attachments), so host automation and UI stay in sync.
- All payloads are JSON with a `schemaVersion`. TypeScript types are generated from or kept in sync with `bridge/schema.json` (CI check).

---

## 8. Data and model plan (with licensing notes)

| Asset | Use | License | Notes |
|---|---|---|---|
| Basic Pitch `nmp.onnx` | Note transcription (MVP) | Apache-2.0 ([repo](https://github.com/spotify/basic-pitch/blob/main/LICENSE)) | Ship the LICENSE; keep attribution in About |
| **Lawrence's own stems + session MIDI** | Ground-truth eval set; later training of ranker/voicing style | Owned by Lawrence **or** label/publisher. **Rights must be confirmed (Q5)** | The most valuable and cleanest asset. Start with 50 clips → 200 |
| POP909 | Chord/progression statistics; voicing priors; v1 training | MIT (GitHub repo); Zenodo lists CC-BY-4.0 ([repo](https://github.com/music-x-lab/POP909-Dataset/blob/master/LICENSE), [Zenodo](https://zenodo.org/records/4245366)) | Keep attribution. Pop-centric |
| Commissioned keyboardist/bassist sessions | Voicing and bass corpus for the learned ranker (LATER) | Work-for-hire contract (need a lawyer) | Budget is an open question |
| Chordonomicon | **Do not use for shipped models** | CC-BY-NC-4.0 ([HF](https://huggingface.co/datasets/ailsntua/Chordonomicon/blob/main/README.md)) | Non-commercial only |
| Lakh MIDI | **Avoid for shipped models** | CC-BY covers the compilation, not the underlying transcriptions ([LMD](https://www.colinraffel.com/projects/lmd/)) | Copyright risk |
| Isophonics / McGill Billboard annotations | Possible eval only | **Unverified, so check the licenses before use** | Annotations only; audio not included |
| mir_eval (Python) | Offline evaluation tooling (dev-time only) | **Verify license** (believed permissive) | Not shipped |

**Labelling format** (`tools/labels/*.json`): `{ "audio": "file.wav", "bpm": 92, "downbeat_sec": 0.12, "key": "F minor", "chords": [{"beat": 0, "len": 4, "symbol": "Fm9"}, …], "source": "vocal|keys|guitar|loop" }`.

**Evaluation:** `tools/eval/eval_chords.py` computes majmin, sevenths and bass accuracy (mir_eval-style) against labels. CI runs it on a small committed fixture set (≤ 10 short clips with cleared rights) and fails if accuracy drops more than 2 points from baseline.

**v1 chord model (LATER):** a CRNN on Basic Pitch salience plus chroma inputs, < 5 MB ONNX (estimate), trained in PyTorch on POP909-derived and Lawrence data. It's exported with opset pinned to the ONNX Runtime version and swapped in behind `IChordRecognizer`.

---

## 9. Repo and folder layout

```
second-opinion/
├─ CMakeLists.txt                 # top-level; options: SO_BUILD_TESTS, SO_ENABLE_AAX (OFF)
├─ CMakePresets.json              # mac-universal-debug/release, win-x64-debug/release
├─ cmake/
│  ├─ CPM.cmake                   # dependency fetch
│  ├─ OnnxRuntime.cmake           # finds/links minimal static ORT per platform
│  └─ Warnings.cmake
├─ external/                      # (or CPM cache) JUCE 9.0.x, Catch2 v3, onnxruntime (prebuilt minimal)
├─ models/
│  ├─ basic_pitch/nmp.onnx  + LICENSE + NOTICE
│  └─ chord/ (v1 later)
├─ src/
│  ├─ plugin/       PluginProcessor.{h,cpp}  PluginEditor.{h,cpp}  Parameters.{h,cpp}
│  ├─ core/
│  │  ├─ audio/     CaptureBuffer  LockFreeFifo  Resampler  AudioFileLoader
│  │  ├─ analysis/  BasicPitchRunner  NoteEvents  Chroma  BeatGrid  ChordDecoder  KeyEstimator
│  │  ├─ theory/    Pitch  Interval  ChordQuality  Chord  ChordSymbolParser  ChordNamer  RomanNumeral  Key
│  │  ├─ reharm/    CandidateGenerator  VoiceLeadingSolver  BassGenerator  Ranker  Explainer
│  │  ├─ midi/      MidiWriter
│  │  ├─ preview/   PreviewSynth
│  │  └─ state/     SessionState  StateMigration
│  ├─ services/     AnalysisService  ReharmService  CacheService
│  └─ bridge/       WebBridge  schema.json  NativeDragStrip
├─ ui/                            # Vite + TypeScript frontend (from Claude Design output)
│  ├─ package.json  vite.config.ts  tsconfig.json
│  ├─ src/ (components/, state/, bridge/ (wraps @juce-framework/webview), styles/tokens.css)
│  └─ dist/ (build output, embedded as BinaryData)
├─ tests/
│  ├─ unit/         theory_*.cpp  chord_decoder_*.cpp  voice_leading_*.cpp  midi_writer_*.cpp  state_*.cpp
│  ├─ integration/  analysis_pipeline_test.cpp (fixture wav → expected chords ± tolerance)
│  ├─ rt/           realtime_safety_test.cpp (alloc-hook / RealtimeSanitizer where available)
│  └─ fixtures/     *.wav (short, rights-cleared), *.json labels
├─ tools/
│  ├─ labels/       label JSONs
│  ├─ eval/         eval_chords.py  requirements.txt
│  └─ train/        (LATER)
├─ docs/            PRD.md  UI_DESIGN_BRIEF.md  adr/0001-webview-ui.md  THIRD_PARTY_LICENSES.md
└─ .github/workflows/ ci.yml  nightly.yml
```

---

## 10. Build, test and CI plan

**Build**
- `cmake --preset mac-universal-release && cmake --build --preset mac-universal-release`
- `cmake --preset win-x64-release && cmake --build --preset win-x64-release`
- `juce_add_plugin(SecondOpinion FORMATS VST3 AU Standalone IS_SYNTH FALSE NEEDS_MIDI_OUTPUT TRUE NEEDS_WEBVIEW2 TRUE …)`. Plugin code and manufacturer codes come from Lawrence (Q1). On Windows, set the WebView2 backend and a user data folder (per the [JUCE WebView guide](https://juce.com/blog/juce-8-feature-overview-webview-uis/)); consider `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1`.
- UI: `cd ui && npm ci && npm run build`. CMake runs this via a custom target before `juce_add_binary_data`.

**Tests**
- Unit (Catch2): theory (parsing and naming round-trips, including all slash chords), chord decoder on synthetic chroma, voice-leading invariants (range, locks, monotonic smoothness/tension), MIDI writer byte-exact golden files, state round-trip and migration, determinism (same seed → same bytes).
- Integration: fixture WAVs → expected chord labels with an accuracy threshold.
- Real-time safety: a test host runs `processBlock` under an allocation hook that fails on any malloc. Enable Clang RealtimeSanitizer where the toolchain supports it (**availability on CI images to verify**).
- **pluginval:** `pluginval --strictness-level 5 --validate <path/to/SecondOpinion.vst3>`. Exit code non-zero fails CI ([pluginval](https://github.com/Tracktion/pluginval/blob/master/README.md), [CI guide](https://github.com/Tracktion/pluginval/blob/develop/docs/Adding%20pluginval%20to%20CI.md)). Strictness 10 runs nightly.
- macOS: `auval -v aufx <subtype> <manu>` for the AU.
- UI: `npm run typecheck && npm run test` (Vitest). Bridge mocks let the UI run in a normal browser (JUCE's frontend library runs without the backend by design).
- Manual DAW matrix per release: Live 12, Logic Pro 12, FL Studio, Studio One/Studio Pro, Reaper, Cubase 15, Bitwig (Pro Tools at the AAX phase).

**CI (GitHub Actions)**
- `ci.yml` on push/PR: matrix {macos-latest, windows-latest} → configure, build Debug + Release, ctest, pluginval (5), auval (mac), UI checks, upload artifacts.
- `nightly.yml`: pluginval 10, eval script, a sanitizer build (ASan/UBSan on mac).
- Release (LATER): codesign + notarize (mac), Authenticode (win), installers (pkg / Inno Setup), AAX PACE wrap.

---

## 11. Milestones: phased tickets (do them in order)

Week numbers follow the 90-day plan. **All durations are estimates.** Each ticket ends with tests passing, pluginval passing (once P1-02 lands) and a small commit series.

### Phase 1: Skeleton and foundations (weeks 1–2)
| Ticket | Description | Acceptance |
|---|---|---|
| P1-01 | Repo scaffold per §9; CMake + presets; fetch JUCE 9.0.x + Catch2; empty pass-through plugin (VST3/AU/Standalone) with name, codes and version | Builds on mac (universal) and win; loads in Reaper; audio passes through |
| P1-02 | CI workflow with build + ctest + pluginval 5 (+ auval on mac) | Green CI on both OSes |
| P1-03 | `core/theory`: Pitch, Chord, ChordSymbolParser, ChordNamer (Pop/Jazz, slash prefs), RomanNumeral, Key | ≥ 150 unit assertions; parse ↔ name round-trip for the full vocab × 12 roots × slash bass |
| P1-04 | WebView editor: Vite TS app "hello" served from BinaryData via ResourceProvider; one bound parameter (tension) via a relay; resizable window with min/max | Knob in UI ↔ host automation both ways; works offline; Windows WebView2 user-data folder set |
| P1-05 | **Spike:** `NativeDragStrip` external `.mid` drag | A dummy C-major MIDI drags into Live, Logic, FL, Reaper, Studio One and Cubase. Results written to `docs/adr/0002-midi-drag.md` |
| P1-06 | Capture: pre-allocated buffer, FIFO, worker drain, PPQ/tempo stamping, Capture/Stop via bridge | Captures 32 bars in Reaper with bar alignment ± 1 sample; RT-safety test passes |
| P1-07 | Drag-in file decode on a worker; BPM prompt fallback | WAV/AIFF/FLAC/MP3 load; UI never blocks > 16 ms |
| P1-08 | ONNX Runtime (minimal, static) + `BasicPitchRunner` on a 22.05 kHz mono buffer → `NoteEvent`s | Fixture WAV → notes match the Python reference within tolerance (onset ± 20 ms, pitch exact, ≥ 90% F-measure vs. Python output); no RT-thread calls |

### Phase 2: Chord reading v0 (weeks 3–4)
| Ticket | Description | Acceptance |
|---|---|---|
| P2-01 | Chroma + bass chroma from salience; beat aggregation | Unit tests on synthetic inputs |
| P2-02 | `ChordDecoder` templates + HMM/Viterbi + confidence + top-3 alts + slash detection | Synthetic tests; integration fixtures ≥ target accuracy (baseline recorded) |
| P2-03 | `KeyEstimator` + Roman numerals | Fixture keys correct; RN tests |
| P2-04 | `AnalysisService` with stages, progress, cancel (< 500 ms), errors | Bridge events visible in the UI stub |
| P2-05 | Eval tooling: label format, `eval_chords.py`, CI accuracy gate | Report printed in CI; gate works |
| P2-06 | UI: chord timeline v1 (bars, segments, confidence shading, RN row), chord popover with alternatives and typed correction | FR-AN-02/03/04 ACs pass |

### Phase 3: Voice-leading engine (weeks 5–6)
| Ticket | Description | Acceptance |
|---|---|---|
| P3-01 | `VoiceLeadingSolver` (beam/DP), range, density, locks, slash bass hard constraints | Invariant tests (range, locks unchanged, smoothness monotonic) |
| P3-02 | `BassGenerator` (root/inversion/walking/pedal) | Tests per mode |
| P3-03 | `MidiWriter` lanes + golden files; drag lanes wired to `NativeDragStrip`; Export to folder | FR-OUT-01/02 ACs |
| P3-04 | `PreviewSynth` (light keys/pad + bass) with snapshot handoff; audition sync to the captured audio | FR-OUT-03, FR-RH-09 ACs; RT-safety test passes |

### Phase 4: Reharm generator (weeks 7–8)
| Ticket | Description | Acceptance |
|---|---|---|
| P4-01 | `CandidateGenerator` for all technique families + melody compatibility | Unit tests per family (e.g., the tritone sub of G7 is D♭7) |
| P4-02 | `Ranker` heuristic + diversity; tension/color/smoothness mapping | Tension monotonicity test; determinism test |
| P4-03 | `ReharmService` + lock/regenerate + history/favorites | FR-RH-01…10 ACs |
| P4-04 | `Explainer` structured → text (Simple/Detailed) | FR-EX-01/02 ACs; snapshot tests of text |

### Phase 5: UX from the design (weeks 9–10)
| Ticket | Description | Acceptance |
|---|---|---|
| P5-01 | Implement the Claude Design output: tokens, layout, all screens/states in `UI_DESIGN_BRIEF.md` | Visual review with Lawrence; all states reachable |
| P5-02 | Settings screen (naming, preview, export, quality, scale, licenses) | FR-SET ACs |
| P5-03 | Keyboard shortcuts, undo/redo, accessibility pass (contrast, focus) | FR-UX-02, NFR-14 |
| P5-04 | Usability test with 5–10 producers; fix the top issues | Notes in `docs/usability-round1.md` |

### Phase 6: Hardening (week 11)
| Ticket | Description | Acceptance |
|---|---|---|
| P6-01 | State versioning + migration + fuzz robustness | pluginval 10 passes nightly |
| P6-02 | Multi-instance (10×) + memory/CPU budget profiling | NFR-04/08/15 met or documented |
| P6-03 | DAW matrix run; fix host quirks | Checklist complete |
| P6-04 | Signing/notarization, installers, third-party licenses page, offline licensing approach (Q8) | Signed builds install cleanly |

### Phase 7: Beta and launch prep (weeks 12–13)
| Ticket | Description | Acceptance |
|---|---|---|
| P7-01 | Closed beta build + feedback form (offline app; feedback is a link in the About page) | 20+ testers |
| P7-02 | Fix the top 10 beta issues | Closed |
| P7-03 | Trial mode (time- or feature-limited, offline) + store assets | Per Q7/Q8 |
| P7-04 | AAX paperwork started in week 1 (Avid dev account, PACE); AAX build if approved | LATER if not ready |

---

## 12. Out of scope for the MVP (reminder)
Stem separation, live harmonization, CLAP/AAX (AAX is targeted right after the MVP), Linux, cloud features, LLM-generated text, learned ranker, artist packs.

---

## 13. Risks

| Risk | Likelihood / impact | Mitigation |
|---|---|---|
| Chord accuracy on real-world audio is too low | High / High | Scope to clean sources; source hint; confidence + alternatives + 2-click correction; ground-truth eval gate; v1 CRNN |
| Suggestions feel generic | Medium / High | Voice-leading cost design; diversity ranking; Lawrence-tuned defaults; taste packs LATER |
| External MIDI drag fails in some hosts (especially from WebView) | Medium / High | Native drag strip (spike P1-05); Export-to-folder fallback |
| ONNX Runtime DLL conflicts / binary size | Medium / Medium | Static minimal build; symbol hiding; size budget check in CI |
| WebView quirks (Windows WebView2 runtime missing, host focus/keyboard issues, multiple instances) | Medium / Medium | Static WebView2 loader; user-data folder per JUCE guidance; one WebView per instance; keyboard focus tests; fallback message if the runtime is missing |
| RT-safety regressions | Low / High | Alloc-hook test in CI; code review rule; no ONNX on the audio thread |
| Data rights (Lawrence's catalog, commissioned sessions) | Medium / High | Written confirmation from rights holders; contracts; avoid NC/scraped datasets |
| Big players add the same feature (Apple, MIK, Scaler) | Medium / Medium | Ship fast; cross-DAW; quality of voicings + explanations; artist brand |
| JUCE licence threshold | Low / Medium | Starter up to $20k; budget Indie ($800 perpetual) before launch |
| Scope creep in UI (Scaler-3-style sprawl) | Medium / Medium | Single-window rule; every new panel needs a user story |

---

## 14. Open questions for Lawrence

1. **Name, branding and IDs:** Is "Second Opinion" final? Company name for the plugin manufacturer field, 4-character manufacturer code and plugin code, bundle ID (e.g., `com.<company>.secondopinion`).
2. **JUCE 9 vs. JUCE 8:** OK to use JUCE 9.0.x (recommended), or must it be 8?
3. **Your DAW and genres:** Which DAW do you use most (first-class testing), and which 2–3 genres should defaults be tuned for?
4. **Ground truth:** Can you provide 50 clean stems (vocal/keys/guitar, 8–32 bars) with chord labels, or 30 minutes with a keys player to label them?
5. **Rights:** Do you control the rights to those stems and session MIDI for training models (vs. label/publisher)?
6. **Preview sound:** Simple synth OK for the MVP, or do you want a signature keys sound (a sampled instrument = size and licensing work)?
7. **Pricing and trial:** Report suggestion is $149 list / $129 intro, perpetual, with a 14–30-day trial. Confirm?
8. **Licensing/copy protection:** Build a simple offline license-key system, or use a third-party licensing service (to be chosen)? Must support offline activation.
9. **Minimum OS:** macOS 12+ and Windows 10+, or newer?
10. **AAX priority:** Is Pro Tools essential for your circle (AAX right after the MVP), or can it wait?
11. **Your taste:** Would you record a "Lawrence pack" of voicings/reharm examples (e.g., 100 progressions) for defaults and a future taste pack?
12. **Beta group:** Who are the 10–20 producers for the closed beta?
