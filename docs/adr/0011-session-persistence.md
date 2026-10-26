# ADR 0011: Session persistence, settings on the native side, and undo sync

Status: accepted (session 3, October 26, 2026)

## Context

FR-ST-01 asks that reopening a DAW project restores the exact view without re-running analysis,
with a versioned state blob, forward migration and pluginval-proof parsing. Captured audio must stay
out of the project (AC3). FR-SET settings lived only in the UI, FR-ST-02 undo only rewound the UI,
and favorites (FR-RH-10) were not saved.

## Decision

**State layout (stateVersion 2).** `getStateInformation` writes `SecondOpinionState` (XML in
JUCE's binary wrapper) with the APVTS parameters and the `Session` tree. `Session` keeps its v1
properties (`ui_settings`, `ui_selected`, `ui_mode`, `ui_section`, `ui_build`, `ui_favorites`,
`uiWidth`, `uiHeight`, `previewSound`) and gains an `Engine` child written by
`SessionEngine::persist()` after every change, so saving only copies the tree under `sessionLock`.

`Engine` holds:

- `Analysis`: grid, bars, key, key confidence; one `Seg` per chord with id, start, length, chord
  (letter, accidental, quality index, optional bass: structural, so spelling round-trips exactly),
  confidence, alternatives, `locked` and `corrected`. Doubles are written with `%.17g`. The melody is
  one string: `t1:` plus base64 of zigzag varints (pitch delta, start delta, length) in ticks of
  1/960 beat, or `d:` text doubles if any value is off that grid. Raw `notes` are not stored (no
  consumer reads them).
- `source`, `sourceName`, `audioHash` (SHA-256 of the analysed samples and rate) and id counters.
- `Generations`: up to 20 recipes (seed, the full `so::reharm::Settings` used, locked ids), the
  current one marked. Suggestions are not stored: the current set is regenerated on load from its
  recipe, which is deterministic. If the analysis changed after that set was made (a correction
  without regenerating), the recipe carries its own `Input` analysis.
- `Song`: the BuildSongRequest JSON, the online MoodProfile JSON when the song was read online,
  the BPM, each section's chosen option, and its own `Idea` analysis when it differs from the
  current one. The song is rebuilt with `SongBridge::build`, then the picks are applied.

**Restore.** `setStateInformation` checks the binary header, UTF-8 validity and the absence of text
nodes before JUCE parses anything, migrates (`1 -> 2` stamps the version; v1 properties are read as
they are), then restores the engine on the message thread (directly, or via `callAsync` when the
host calls from another thread). Every field is range-checked; an invalid analysis drops the whole
engine state rather than half-loading it. When an editor is open, or when its page next loads,
`replay()` re-sends `analysisReady`, `suggestionsReady` (with `selectedId`) and `songReady` (with
`restored: true`), and the UI shows the saved card, mode, section and favorites.

**Audio cache.** Captured clips are hashed and written to
`<userApplicationData>/SecondOpinion/cache/<sha256>.flac` (mono, 24 bit) on a dedicated worker
thread. Only the hash goes into the project. `clearCache()` deletes the folder's contents; a missing
file never blocks loading, because nothing reads it yet (audition of original audio is not built).

**Settings.** New native function `setSettings(settings)` receives the UI's Settings object.
C++ honours `quality` and `sourceHint` (AnalyzerOptions), `previewSound`, `ppq` (480 or 960) and
`velocity` (flat = 100, humanized = the voicer's accents plus -6..+6 seeded by the progression id)
for MIDI export, and stores the object as `ui_settings`. Notation, spelling and slash preference stay
UI-only: the bridge always carries Pop symbols.

**Undo.** The UI keeps its 50-step undo stack. After undo or redo it calls `restoreSnapshot` with
the segment chords and locks, the generation id of the suggestion set and the song id with its
picks. The engine keeps the last 64 generated sets and songs in memory, so it switches back without
recomputing. `suggestionsReady.generation` and `Song.generation` carry those ids.

## Consequences

- Projects load in about the time of one suggestion generation plus one song build, with no ONNX
  or DSP work, and the blob stays small (a 4-bar session is a few kilobytes).
- Older generation recipes replay against the current analysis; only the current set is exact
  after reload. The UI undo history starts empty after reload.
- Split and merge in the timeline are still UI-only; `restoreSnapshot` ignores ids it does not know.
- Tests: `tests/rt/session_state_test.cpp` (round-trip, v1 migration, 500 fuzzed blobs, settings and
  MIDI bytes, undo, cache) and the Vitest contract tests.
