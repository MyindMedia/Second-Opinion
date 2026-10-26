# Second Opinion: notes for Claude Code sessions

Read `docs/PRD.md` (source of truth), `docs/UI_DESIGN_BRIEF.md`, `docs/CLAUDE_BUILD_PROMPT.md`
(working rules) and `STATUS.md` (where things stand) before changing anything.

- Build: `cmake --preset <mac-universal|win-x64|linux>-debug && cmake --build --preset <same> && ctest --preset <same>`
- UI: `cd ui && npm ci && npm run dev` (mock bridge), `npm test`, `npm run typecheck`
- Validate: pluginval strictness 5 on the VST3; `auval -v aufx SOp1 Brmt` on macOS.
- `src/core/**` must not include JUCE. Keep it headless and unit-tested.
- Nothing in `processBlock` may allocate, lock, log or do I/O. Extend `tests/rt` when touching it.
- No network calls anywhere. No CDN fonts or remote URLs in `ui/`.
- Randomness always takes an explicit seed.
- Parameter IDs in `src/plugin/Parameters.h` and `ui/src/bridge/juce.ts` must match and never change.
- Design tokens live in `ui/src/styles/tokens.css` and are mirrored in `src/plugin/Theme.h`.
- JUCE `String` from a `const char*` must be ASCII; use `juce::String::fromUTF8` for anything else.
- Lawrence prefers documents and messages without em dashes.
- Test case names must be ASCII (CTest on Windows mangles non-ASCII names and the test is not found).
