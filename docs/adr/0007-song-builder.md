# ADR 0007: Song builder (mood engine, structure, section options)

Status: accepted (feature FR-SB, PRD section 5.9)

## Context

Lawrence asked for the plugin to listen to an uploaded sample or song idea and recommend chords
and progressions to build out the whole song, driven by mood and a free-text description. He chose
an offline mood engine by default plus an optional online AI interpreter. Another engineer owns the
network call; this ADR defines the engine, the interface and the JSON contract.

## Decision

Headless C++20 in `src/core/song/` (namespace `so::song`, no JUCE, deterministic per seed):

| File | Role |
|---|---|
| `SongTypes.h` | Enums with stable string names: `Style`, `Section`, `MoodChip` (16), `ColorFamily`, `StructurePreset`, `IdeaRole`. |
| `MoodProfile.h` | `MoodProfile` + `MoodOverride` (per-section partial), `toJson`, `moodFromJson`, `kMoodProfileJsonSchema`, `moodSystemPrompt()`, `moodUserPrompt()`. |
| `MiniJson.h` | 300-line JSON reader/writer. No dependency, no exceptions, accepts Markdown code fences. |
| `MoodInterpreter.h` | Offline text + chips to `MoodProfile`. Lexicon in `MoodLexicon.inc`. |
| `SongStructure.h` | Structure presets and display names. |
| `SongBuilder.h` | `SongBuilder::build / choose / relink`, `transitionBetween`. Templates in `HarmonyLibrary.inc`. |
| `SongMidi.h` | Song or section to `TimedChord`s, voiced lanes, and `{symbol, beats}` for the drag strip. |

Both interpreter paths meet at `MoodProfile`:

```
text + chips --MoodInterpreter (offline, default)--> MoodProfile --SongBuilder--> Song
text + chips --moodSystemPrompt + AI (opt-in)--> JSON --moodFromJson--^
```

### Lexicon design (offline interpreter)

- About 540 phrases in six themes (dark and sad; gritty and aggressive; tension and mystery;
  romance and nostalgia; hopeful and bright; calm, dreamy and space; soul and sophistication;
  energy and motion; genres). Each row is a delta on the five dimensions plus an optional style
  vote (with weight), preferred color families and a harmonic-rhythm hint. Deltas add to the neutral
  profile (valence 0, energy 0.5, tension 0.3, darkness 0.3, sophistication 0.4) and are clamped.
- Tokenizer: lower case, apostrophes dropped (`isn't` -> `isnt`), dashes split (`late-night` ->
  `late night`), `r&b` kept whole. Punctuation and the words but, then, while, though, however
  split clauses. Matching is longest phrase first (up to 4 words), with a light stemmer fallback
  (darker, moodier, happier).
- Modifiers look back up to 3 tokens in the clause: negators (not, no, never, without, less...)
  turn a term into half of its opposite, so "not too dark" lowers darkness a little instead of
  inverting it; intensifiers multiply (very 1.5, super 1.6, slightly 0.5, kinda 0.6...).
- Section binding: section words (verse, chorus, hook, refrain, drop, pre, pre-chorus, build, bridge,
  middle 8, intro, outro, breakdown...) bind each mood term in the same clause to the nearest section
  word; ties go to the following word; an "and" between them adds distance, so "dark verse and hopeful
  chorus" and "a bridge that feels like sunrise and a soft outro" bind correctly. Strong genre words
  always apply to the whole song. Section overrides store absolute values (global + section deltas).
- Style: the highest weighted vote wins if it reaches 0.5 (first seen breaks ties), else Auto.
  Chips are global terms; the Cinematic chip adds a weak style vote.

### JSON contract for the AI path

Schema id `second-opinion/mood-profile/v1` (full text in `kMoodProfileJsonSchema`):

```json
{
  "schemaVersion": 1,
  "valence": -0.35,          // -1 sad .. 1 happy, default 0
  "energy": 0.35,            // 0..1, default 0.5
  "tension": 0.4,            // 0..1, default 0.3
  "darkness": 0.8,           // 0..1, default 0.3
  "sophistication": 0.7,     // 0..1, default 0.4
  "style": "RnB",            // Auto|RnB|NeoSoul|HipHop|Trap|Drill|Pop|Gospel|LoFi|Cinematic|Rock|EDM|Afrobeats|Latin|Jazz|Country
  "chordsPerBar": 0,         // 0 = style default, else 0.25..4
  "colors": ["Extensions"],  // Diatonic|Borrowed|SecondaryDominants|TritoneSubs|ChromaticMediants|Suspensions|Extensions|Modal|Pedal
  "sections": {              // keys: Intro|Verse|PreChorus|Chorus|PostChorus|Bridge|Breakdown|Outro (aliases hook, pre accepted)
    "Chorus": { "valence": 0.2, "darkness": 0.55 },
    "Bridge": { "valence": 0.5, "darkness": 0.3, "energy": 0.55 }
  }
}
```

(Comments are for this document only; the real document is plain JSON.) Every field is optional.
`moodFromJson` ignores unknown keys, unknown color and section names, clamps numbers, maps an unknown
style to Auto, and fails only on malformed JSON or wrong value types, so the integrator can fall back
to the offline interpreter. The integrator sends `moodSystemPrompt()` as the system message (it
embeds the schema and a worked example) and `moodUserPrompt(text, chips)` as the user message.

### Builder

1. Resolve style (Auto picks from the mood), key (the idea's key; with no idea, the idea's tonic
   with minor when darkness > 0.5 or valence < -0.15), tempo (host override, then idea, then style),
   and the section list.
2. The idea's section type (Verse, Chorus for Chorus/Hook, Verse for Loop; first available type if the
   form lacks it) holds the heard chords exactly, re-based to beat 0 and tiled to the section length.
   With no idea that section is generated first and its first loop becomes the backbone.
3. Other sections are generated once per type, in the order Chorus, Verse, Pre-Chorus, Bridge,
   Post-Chorus, Breakdown, Intro, Outro, so later sections can contrast with earlier ones.
   Candidates come from:
   - 67 hand-written templates (Roman numerals against the major scale of the tonic, so minor
     templates read bIII, bVI, bVII), tagged with idiomatic styles, brightness, tension and energy;
   - transformations of the backbone: richer colors, relative-major swap, walking-bass inversions,
     approach chords into every change, rotation, reduction, tonic pedal;
   - for intro and outro: the loop, the loop over a pedal, a two-chord teaser, a sus2 start, a final
     cadence (Picardy third when the mood and style allow), plagal or Aeolian endings.
4. Templates are colored by a scale-aware colorizer: the color level is 0.55 x sophistication +
   0.45 x the style's typical sophistication; 7ths and 9ths are added only when they belong to the
   key (or the parallel key for borrowed chords), so Cm in F minor gets Cm7, never Cm9.
5. Scoring: style fit, distance to the section's target brightness
   (0.8 x valence - 0.9 x (darkness - 0.3)), tension and energy, preferred color families, contrast
   with earlier sections (identical sequences are penalized, bridges reward new chords), a bonus for
   ending on a chord that leads into the next section, a Loop bonus for backbone variations, and
   seeded noise (plus or minus 0.3). The top options are picked greedily with distinct chord
   sequences and ids, and a penalty for repeating a first chord.
6. Section moods: the chorus gets +0.15 energy and lifts by brightness, or by tension when darkness
   is above 0.6; the pre-chorus gets +0.25 tension; a section the user described as happier than
   the song gets an extra brightness lift.
7. `relink` computes keys (a chosen bridge option can move later sections up 1 or 2 semitones),
   copies repeats from their first instance, then turns each non-idea option's last chord toward the
   next section's first chord if it does not already lead there. Approach chords are style-specific:
   gospel V7sus4 to V7, R&B and jazz ii-V or tritone sub, EDM Vsus4 to V, cinematic backdoor bVII,
   trap and drill Phrygian bII or bVII, pop V. Then Roman numerals, explanations (why, color note,
   transition sentence), tension and the arc paragraph are rebuilt.

## Consequences

- One engine for both interpreter paths; the AI can only change numbers in a validated document.
- Lexicon and templates are data (`.inc` files) that can grow without touching logic.
- A key change never moves the heard idea, so it is not offered when the idea follows the bridge.
- The placeholder voicer (`so::midi::voiceProgression`) is used for lanes until the voice-leading
  solver lands; swapping it in needs no API change.
- The online path is the product's only possible network call and needs Lawrence's explicit
  approval as an exception to FR-UX-01. Nothing in `src/core` performs network I/O.
