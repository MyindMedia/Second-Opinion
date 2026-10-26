# Third-party licenses

Shipped in the plugin:

| Component | License | Notes |
|---|---|---|
| JUCE 9 | JUCE 9 licence (Starter tier: free up to USD 20k revenue or funding in the prior 12 months) or AGPLv3 | Upgrade to Indie before revenue passes the Starter limit (open question Q14). |
| VST 3 SDK | MIT (since 3.8.0) | Included through JUCE. |
| @juce-framework/webview | AGPL-3.0-only OR JUCE licence | Same terms as JUCE. |
| IBM Plex Sans, Plex Mono, Plex Sans Condensed | SIL Open Font License 1.1 | `docs/licenses-ibm-plex-OFL.txt` |
| Noto Music (subset: ♭ ♮ ♯ 𝄪 𝄫) | SIL Open Font License 1.1 | `docs/licenses-noto-music-OFL.txt`. Subset with pyftsubset; referenced in CSS as "SO Accidentals". |

Planned (not yet included):

| Component | License | Ticket |
|---|---|---|
| Basic Pitch model `nmp.onnx` | Apache-2.0 (ship LICENSE and NOTICE) | P1-08 |
| ONNX Runtime (minimal static build) | MIT | P1-08 |

Development only (not shipped): Catch2 (BSL-1.0), CPM.cmake (MIT), Vite (MIT), TypeScript
(Apache-2.0), Vitest (MIT), pluginval (GPLv3, run as a separate tool).

The About tab in Settings lists the shipped components. Keep this file and that tab in sync.
