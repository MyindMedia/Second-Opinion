# ADR 0004: Dependency versions

Status: accepted (Phase 1)

| Dependency | Version | Why this version |
|---|---|---|
| JUCE | 9.0.4 (tag) | Latest 9.0.x at the time of writing. The PRD recommends JUCE 9; every API used here also exists in 8.x. |
| Catch2 | 3.7.1 | Stable v3 with `catch_discover_tests`. |
| CPM.cmake | 0.40.2 | Pins dependencies by tag; `CPM_<name>_SOURCE` allows offline builds from local checkouts. |
| @juce-framework/webview | 1.0.0 | The official TypeScript interop package published with JUCE 9.0.1. |
| Vite | 7.3.1 | Chosen over newer 7.3.x releases published less than two weeks before this session. |
| TypeScript | 5.9.3 | Stable 5.x. |
| Vitest | 3.2.4 | Matches Vite 7. |
| @fontsource IBM Plex Sans / Mono / Sans Condensed | 5.2.x | Bundled locally (SIL OFL 1.1). |

Build choices:

- macOS presets use the Xcode generator (always present with Xcode), universal `arm64;x86_64`,
  deployment target 12.0 (open question Q9).
- Windows presets use Visual Studio 17 2022, x64, and link WebView2 statically
  (`NEEDS_WEBVIEW2`, `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1`).
- The plugin is an effect (`aufx`) with MIDI output, not a MIDI effect, because it must pass audio
  through untouched while capturing.
