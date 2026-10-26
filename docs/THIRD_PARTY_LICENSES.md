# Third-party licenses

Shipped in the plugin:

| Component | License | Notes |
|---|---|---|
| JUCE 9 | JUCE 9 licence (Starter tier: free up to USD 20k revenue or funding in the prior 12 months) or AGPLv3 | Upgrade to Indie before revenue passes the Starter limit (open question Q14). |
| VST 3 SDK | MIT (since 3.8.0) | Included through JUCE. |
| @juce-framework/webview | AGPL-3.0-only OR JUCE licence | Same terms as JUCE. |
| IBM Plex Sans, Plex Mono, Plex Sans Condensed | SIL Open Font License 1.1 | `docs/licenses-ibm-plex-OFL.txt` |
| Noto Music (subset: ♭ ♮ ♯ 𝄪 𝄫) | SIL Open Font License 1.1 | `docs/licenses-noto-music-OFL.txt`. Subset with pyftsubset; referenced in CSS as "SO Accidentals". |
| Basic Pitch model `nmp.onnx` (Spotify AB, icassp_2022) | Apache-2.0 | `models/basic_pitch/LICENSE` and `models/basic_pitch/NOTICE` ship with the model in `Resources/basic_pitch/`. The NOTICE must be reproduced in About. ADR 0009. |
| ONNX Runtime 1.21.0 (Microsoft) | MIT | Prebuilt shared library for now (minimal static build planned, ADR 0009). Its `LICENSE` and `ThirdPartyNotices.txt` ship in `Resources/licenses/onnxruntime/`. |

Development only (not shipped): Catch2 (BSL-1.0), CPM.cmake (MIT), Vite (MIT), TypeScript
(Apache-2.0), Vitest (MIT), pluginval (GPLv3, run as a separate tool). `tools/ml` (fixture
generation): basic-pitch Python package (Apache-2.0), onnxruntime and onnx (MIT/Apache-2.0),
numpy and scipy (BSD), librosa (ISC), mir_eval and pretty_midi (MIT), resampy (ISC),
scikit-learn (BSD).

The About tab in Settings lists the shipped components. Keep this file and that tab in sync.
