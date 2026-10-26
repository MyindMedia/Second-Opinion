# ADR 0010: Opt-in online AI for song descriptions

Status: accepted (Lawrence chose "Both" on October 26, 2026)

## Context

Build out mode reads a typed description ("dark late-night R&B, hopeful chorus") plus mood chips.
The PRD's FR-UX-01 says the product makes no network calls at all. Lawrence asked for an offline mood
engine by default and an online AI interpreter as an option.

## Decision

- The offline mood engine (`so::song::MoodInterpreter`, 545-phrase lexicon, ADR 0007) is the default
  and always available. The plugin works fully offline unless the user turns the option on.
- Settings > AI holds an "Use online AI" toggle and an API key field. The toggle in Build out is
  disabled until a key is stored.
- When on, `AiMoodClient` (src/services) sends ONLY the description text and chip names
  (`so::song::moodUserPrompt`) with `so::song::moodSystemPrompt()` to the Claude Messages API, and
  expects a MoodProfile JSON document (`kMoodProfileJsonSchema`). Audio, chords and project data are
  never sent.
- Any failure (no network, rejected key, unusable reply) falls back to the offline engine, the song is
  still built, and the UI shows an info toast (`ai_unavailable` or `ai_key_invalid`).
- The key is stored in the user's application-data folder (`SecondOpinion/ai.key`, owner-only
  permissions on macOS and Linux), never in the DAW project, so sharing a project never shares a key.
  The bridge never returns the key; it returns a masked form.
- The model id defaults to a fast, low-cost Claude model and is configurable in code.

## Consequences

- FR-UX-01 now reads: no network calls unless the user enables online AI in Settings. The CI check
  for external URLs covers the UI bundle only; the C++ client is the single network path.
- Linux builds keep `JUCE_USE_CURL=0`, so the online option is a no-op there (Linux is dev-only).
- Offline license activation (NFR-10) is unaffected.
