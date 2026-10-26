# WebView bridge contract, schemaVersion 1

The C++ side implements this; the UI codes against it. The TypeScript source of truth is
`ui/src/bridge/contract.ts` (types, `NATIVE_FUNCTIONS`, `EVENTS`, runtime guards). Keep the two in sync
and bump `schemaVersion` on any breaking change. This supersedes the list in PRD section 7.9.

## Mechanics

- **Functions (JS to C++)** are registered with `WebBrowserComponent::Options::withNativeFunction (name, fn)`
  under the exact names below. Arguments arrive positionally in `juce::Array<juce::var>`. Call
  `complete (result)` with the JSON result (a `juce::var` built from `DynamicObject`s).
- **Events (C++ to JS)** are sent with `emitEventIfBrowserIsVisible (name, payload)`. The payload is
  the object itself (not wrapped).
- **Long work never blocks a function.** Capture, analysis, generation, song building and audition
  return an `Ack` at once and report through events, from the message thread.
- **Incremental integration.** The UI checks `window.__JUCE__.initialisationData.__juce__functions`.
  Any function not registered falls back to the in-UI mock (`ui/src/bridge/mock-backend.ts`), and the
  UI listens to events from both. You can land functions one at a time and the plugin keeps working.
- **Strings.** Chord symbols use Pop spelling with real accidentals (U+266D flat, U+266F sharp), for
  example `"D♭maj7/F"`. Build them with `juce::String::fromUTF8`, never from a raw `const char*`.
- **Ids** are strings. A C++ `int` id serialises as its decimal string (`"12"`).
- **Beats** are quarter notes; beat 0 is bar 1, beat 1 of the capture (`BeatGrid` beat 0).
- **Enumerations** are lowercase ASCII ids. The UI owns all human labels.
- **Copy** in `message`, `explanation`, `title` fields is shown as is: plain verbs, sentence case,
  no em dashes.

## Native functions

| Function | Arguments | Result | Notes |
|---|---|---|---|
| `startCapture` | none | `Ack` | Arms capture. Emits `captureState` (armed, recording, done), then analysis events. |
| `stopCapture` | none | `Ack` | Stops. With 2 or more bars recorded, analysis starts (`captureState: done`); otherwise `idle`. |
| `setTempo` | `bpm: number` (40 to 240) | `Ack` | Answer to `captureState.needsTempo` or error `no_tempo`; then analysis runs. |
| `openAudioFile` | none | `OpenFileResult` | Shows the native file chooser. `{ ok: false, cancelled: true }` if closed. On `ok`, analysis runs and reports through events. |
| `analyzePcm` | `AnalyzePcmRequest` | `Ack` | Audio the UI decoded from a drag and drop. See "PCM payload". |
| `cancel` | none | `Ack` | Cancels capture, analysis or song building. No further events for that job. |
| `setChord` | `segmentId: string, symbol: string` | `SetChordResult` | Typed correction. Store it, mark `userCorrected` and `userLocked`, return the normalized Pop symbol. Invalid: `{ ok: false, error }`. |
| `lock` | `segmentIds: string[], locked: boolean` | `Ack` | Locked segments stay exactly as heard in every generated set. |
| `generate` | `{ seed: number }` | `Ack` | Regenerates suggestions with current parameters. Emits `suggestionsReady`. |
| `audition` | `AuditionRequest` | `Ack` | Starts or stops the preview synth. Emits `auditionState` while playing. |
| `buildSong` | `BuildSongRequest` | `Ack` | Builds a song from the current analysis. Emits `progress` (stage `song`), then `songReady` or `error`. |
| `chooseSectionOption` | `sectionIndex: number, optionIndex: number` | `Ack` | Records the user's pick in the song state (used by `audition` and `exportMidi`). |
| `setProgression` | `ProgressionPayload` | `SetProgressionResult` | Existing. Content of the native drag strip. In Build out the UI sends the chosen arrangement (one section or the whole song) as back-to-back chords. |
| `exportMidi` | `scope: 'current' \| 'section' \| 'song'` | `Ack` | Existing, now with a scope. `current` writes what `setProgression` last sent (also the behaviour with no argument). |
| `getSession` | none | `Record<string, string>` | Existing. Keys the UI reads: `ui_settings`, `ui_build`, `ui_selected`, `ui_mode`, `ui_section`, `ui_favorites` (JSON array of suggestion ids). |
| `setSessionValue` | `key: string, value: string` | `boolean` | Existing. Only `ui.*` keys; `.` becomes `_` in the stored name. |
| `setAiKey` | `key: string` | `AiStatus` | Stores the key in the user's application-data folder (see `AiMoodClient::setKey`). Empty string removes it. Never echo the key back; return `maskedKey`. |
| `getAiStatus` | none | `AiStatus` | Called at boot. |
| `parseChord` | `symbol: string` | `ParseChordResult` | Existing. |
| `setSettings` | `SettingsPayload` (the UI's whole Settings object) | `Ack` | C++ honours `quality` (fast, balanced, accurate) and `sourceHint` (auto, vocal, keys, guitar, loop) for analysis, `previewSound` (keys, pad, off), and MIDI export `ppq` (480 or 960) and `velocity` (flat, humanized). Unknown values fall back to the defaults. The object is saved as `ui_settings` and read back when a project loads. Not an object: `{ ok: false }`. |
| `clearCache` | none | `{ ok, removed }` | Deletes the captured-audio cache (`<user app data>/SecondOpinion/cache`). Projects keep only the hash, so they still load. |
| `restoreSnapshot` | `SnapshotPayload` | `Ack` | Undo and redo (FR-ST-02). Puts the native session back to a UI snapshot: `segments` (id, ASCII symbol, confidence, userLocked, userCorrected; unknown ids ignored), `generation` (a `suggestionsReady.generation`), `song` (`null` for none, or `{ generation?, chosen[] }`). No events. `ok: false` when an id is no longer known. |

## Events

| Event | Payload | When |
|---|---|---|
| `progress` | `{ stage, pct }` | `stage` is `preparing`, `transcribing`, `harmony`, `suggestions` (analysis, in order) or `song` (Build out). `pct` 0 to 100 within the stage. |
| `captureState` | `{ state, bar, level, needsTempo, maxBars? }` | `state`: `idle`, `armed`, `recording`, `done`. `bar` is 1-based while recording. `level` 0 to 1. `maxBars` defaults to 32. Send at least 10 per second while recording. |
| `analysisReady` | `AnalysisResult` | Analysis finished. Follow it with `suggestionsReady` for seed 0. |
| `suggestionsReady` | `{ schemaVersion, seed, suggestions: Suggestion[], generation?, selectedId? }` | After analysis and after every `generate`. `generation` identifies the set for `restoreSnapshot`. `selectedId` appears only on replay of a saved project. |
| `songReady` | `Song` | `buildSong` finished. On replay of a saved project it carries `restored: true` (show it without an undo step). `generation` identifies the song for `restoreSnapshot`. |
| `auditionState` | `{ playing, beat, target?, id? }` | About 20 per second while playing, once with `playing: false` when it stops. `beat` is in song beats for `section` and `song`, capture beats for `original` and `suggestion`. |
| `transport` | `{ playing, bpm, ppq, numerator, denominator, hasTempo }` | Existing. |
| `error` | `{ code, message }` | See error codes. |

## Shapes

### AnalysisResult (from `so::analysis::AnalysisResult`)

```json
{
  "schemaVersion": 1,
  "source": "captured",
  "sourceName": "Lead vocal",
  "bars": 32,
  "bpm": 92,
  "beatsPerBar": 4,
  "downbeatSec": 0.12,
  "key": "F minor",
  "keyConfidence": 0.86,
  "segments": [
    { "id": "0", "startBeat": 0, "beats": 4, "symbol": "Fm9", "confidence": 0.91,
      "roman": "i9", "fn": "tonic",
      "alternatives": [{ "symbol": "Fm7", "confidence": 0.05 }],
      "userLocked": false, "userCorrected": false }
  ],
  "melody": [{ "pitch": 72, "startBeat": 0, "beats": 1.5 }]
}
```

Mapping: `grid.bpm` to `bpm`, `grid.beatsPerBar` to `beatsPerBar`, `grid.downbeatSec` to
`downbeatSec`; `theory::Chord` to its Pop symbol; `roman` and `fn` (`tonic`, `predominant`,
`dominant`, `color`) relative to `key`; `key` written as `"<Tonic> major|minor"` with real
accidentals. `notes` (raw transcription) is not sent; use the resource provider if the UI ever needs it.
`source` is `file` for `openAudioFile` and `analyzePcm`.

### Suggestion

Unchanged from `ui/src/state/types.ts`: `{ schemaVersion, id, title, techniques[], tension, movement,
summary, chords: [{ id, startBeat, beats, symbol, roman, fn, changed, why?: { plain, term, detail? } }],
containsLocked? }`. `techniques` use the display names `Diatonic`, `Borrowed`, `Secondary`,
`Tritone sub`, `Chromatic mediant`, `Neo-Riemannian`.

### BuildSongRequest

```json
{
  "schemaVersion": 1,
  "description": "Dark late-night R&B, hopeful chorus",
  "moods": ["dark", "hopeful"],
  "ideaSection": "verse",
  "structure": "auto",
  "seed": 0,
  "useOnlineAi": false
}
```

- `moods`: any of `dark hopeful melancholic euphoric dreamy tense romantic aggressive chill nostalgic
  cinematic soulful mysterious triumphant bittersweet gritty`. May be empty.
- `ideaSection`: `verse`, `chorus`, `hook`, `loop`. Which part of the song the user's audio is.
- `structure`: `auto`, `pop`, `rnb`, `hiphop-trap`, `gospel`, `edm`, `cinematic`. `auto` lets the
  mood engine choose from moods and description.
- `seed`: Shuffle increments it. Same request plus same analysis must give the same song.
- `useOnlineAi`: the UI sends `true` only when the user turned it on and `getAiStatus().hasKey`. Even
  then, any AI failure must fall back to the offline mood engine and still produce a song; send
  `error { code: "ai_unavailable" }` first so the UI can say so. Only the description text goes online.

### Song

```json
{
  "schemaVersion": 1,
  "id": "song-1a2b-0",
  "key": "F minor",
  "bpm": 92,
  "beatsPerBar": 4,
  "structure": "rnb",
  "moods": ["dark", "hopeful"],
  "interpreter": "offline",
  "interpretedAs": "Dark, hopeful R&B with your idea as the verse",
  "arc": "Low, close verses that open into a warm chorus, with a short bridge before the last lift.",
  "sections": [
    {
      "section": "chorus",
      "label": "Chorus 1",
      "startBar": 12,
      "bars": 8,
      "chosen": 0,
      "isIdea": false,
      "options": [
        {
          "title": "Lift to the relative major",
          "explanation": "Leans on the major chords of the key, so the chorus sounds brighter than the verse without changing key.",
          "energy": 80,
          "tension": 40,
          "chords": [{ "startBeat": 0, "beats": 4, "symbol": "A♭maj7", "roman": "IIImaj7", "fn": "tonic" }],
          "transition": "Optional. Ends on E♭, which pulls into Fm9 at the top of Verse 2."
        }
      ]
    }
  ]
}
```

Rules the UI relies on:

- `sections` are in song order and contiguous: `startBar` of each equals the previous `startBar + bars`.
- `section` kinds: `intro verse pre chorus hook post bridge build drop breakdown vamp outro loop`.
  `label` is unique and numbered when a kind repeats (`Verse 1`, `Verse 2`).
- `chords[].startBeat` is **relative to the section start**. Each option's chord beats sum to
  `bars * beatsPerBar`. The UI stretches a chord over any gap, but do not rely on it.
- `isIdea` marks every section built from the user's audio. Its first option should be the chords as
  heard (title "As you played it").
- Usually 3 options per section. `chosen` is the default pick.
- `energy`, `tension` are 0 to 100. `transition` is optional; without it the UI writes one from the
  last chord's function and the next section's first chord.

### AuditionRequest

`{ target: 'original' | 'suggestion' | 'section' | 'song', id?: string, play: boolean }`

- `suggestion`: `id` is the suggestion id. `original`: no id.
- `section`: `id` is `"<sectionIndex>"` to play the chosen option, or `"<sectionIndex>.<optionIndex>"`
  to preview another option without choosing it. Loop the section.
- `song`: plays the chosen arrangement from bar 1, looping.

### AiStatus

`{ hasKey: boolean, maskedKey: string, lastError?: string }`. `maskedKey` like `"sk-ant-...abcd"`
(`AiMoodClient::maskedKey`). Set `lastError` to a plain sentence when a key is rejected or the last
request failed; clear it on success.

## PCM payload (`analyzePcm`)

The UI decodes a dropped file with Web Audio (`decodeAudioData`), downmixes to mono and resamples to
**22050 Hz** with an `OfflineAudioContext`, caps it at **180 seconds**, and sends:

```json
{ "schemaVersion": 1, "sampleRate": 22050, "samplesBase64": "AACAPwAAAL8...", "name": "Hook idea.wav" }
```

- `samplesBase64`: float32 **little-endian** mono, standard base64 with padding (RFC 4648).
  Decode with `juce::Base64::convertFromBase64 (MemoryOutputStream&, text)` into a float buffer;
  sample count is `bytes / 4`. Do not use `MemoryBlock::fromBase64Encoding`, which expects JUCE's
  own size-prefixed format, not RFC 4648.
- `sampleRate` is normally 22050. If an old WebView cannot resample, the UI sends the decoded rate
  instead, so always honour the field (wrap in `so::analysis::AudioClip { samples, sampleRate }`).
- Size: 180 s is 3,969,000 samples, 15.9 MB raw, about 21.2 MB of base64 in one call. Decode off the
  message thread and return the `Ack` immediately.
- Longer than 180 s: `Ack { ok: false }` and `error { code: "too_long" }`. Under 1 s: `decode_failed`.

## Error codes

| Code | UI behaviour |
|---|---|
| `capture_failed` | Error screen "Capture didn't start" with your message. |
| `no_tempo` | Tempo prompt; UI calls `setTempo`. |
| `decode_failed`, `too_long`, `no_pitch`, `analysis_failed` | Error screen with Retry and Open audio file. |
| `generate_failed` | Toast; Generate becomes available again. |
| `song_failed` | Build out error state with Try again. |
| `ai_unavailable`, `ai_key_invalid` | Info toast; the song still arrives (offline fallback). |
| `export_failed`, `busy` | Toast. |

## Sequences

- **Capture:** `startCapture` then `captureState armed`, `recording` (bar 1..N), `done`, `progress`
  (4 stages), `analysisReady`, `suggestionsReady`.
- **Drag and drop:** UI decodes (its own "Decoding file" stage), `analyzePcm`, then `progress`,
  `analysisReady`, `suggestionsReady`.
- **Open audio file:** `openAudioFile` (native chooser), then as above.
- **Correct a chord:** `setChord` result, then `generate` as usual. Locks: `lock`.
- **Build out:** `buildSong`, `progress { stage: "song" }`, `songReady`. Picking an option:
  `chooseSectionOption`, then `setProgression` with the arrangement for the drag strip.
- **Audition:** `audition { play: true }`, `auditionState` ticks, `audition { play: false }`.
- **Project reopened (FR-ST-01):** the plugin restores its session from the DAW project without
  re-running analysis. After the page loads, C++ replays `analysisReady`, `suggestionsReady` (with
  `selectedId`) and `songReady` (with `restored: true`). The UI reads `ui_mode`, `ui_section` and
  `ui_favorites` from `getSession`. Storage details: `docs/adr/0011-session-persistence.md`.
- **Undo or redo:** the UI rewinds its own state, then calls `restoreSnapshot` once.
- **Settings change:** `setSettings` with the whole Settings object (also `setSessionValue ui.settings`).

## Testing

`ui/src/bridge/contract.test.ts` runs every flow above against the mock with fake timers and checks
the payloads with `isAnalysisResult`, `isSuggestionsReady` and `isSong`. Running the same assertions
against a real build is the acceptance test for the C++ side. The browser preview accepts
`?state=build`, `build-form`, `build-song`, `build-section` (`&section=2&option=1`), `build-loading`,
`build-error`, `build-ai` (`&key` to preview a stored key), `decoding`, plus the earlier presets.
