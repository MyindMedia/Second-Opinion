# Open questions for Lawrence

Placeholders in use until answered:

| Item | Placeholder | Where |
|---|---|---|
| Company | Berment Audio | `CMakeLists.txt` (`COMPANY_NAME`) |
| Manufacturer code | Brmt | `PLUGIN_MANUFACTURER_CODE` |
| Plugin code | SOp1 | `PLUGIN_CODE` |
| Bundle ID | com.bermentaudio.secondopinion | `BUNDLE_ID` |
| Minimum macOS | 12.0 | `CMAKE_OSX_DEPLOYMENT_TARGET`, mac presets |

Changing the manufacturer or plugin code after release breaks saved projects, so decide these
before the first beta.

From the PRD (section 14):

1. Is "Second Opinion" final? Company name, 4-character manufacturer code, plugin code, bundle ID.
2. JUCE 9 vs 8: this build uses JUCE 9.0.4. OK to keep?
3. Which DAW do you use most (first-class testing), and which 2 to 3 genres should defaults suit?
4. Ground truth: can you provide 50 clean stems with chord labels, or 30 minutes with a keys player?
5. Rights: do you control the rights to those stems and session MIDI for model training?
6. Preview sound: simple synth for the MVP, or a signature sampled keys sound?
7. Pricing and trial: USD 149 list / 129 intro, perpetual, 14 to 30 day trial?
8. Licensing: simple offline license keys, or a third-party service (must support offline activation)?
9. Minimum OS: macOS 12+ and Windows 10+, or newer?
10. AAX priority: needed right after the MVP, or can it wait?
11. Would you record a "Lawrence pack" of voicings and reharm examples?
12. Who are the 10 to 20 beta producers?

New from Phase 1:

13. Signing: do you have an Apple Developer ID (for notarized macOS builds) and a Windows code-signing
    certificate? Unsigned builds work locally but not for beta testers.
14. JUCE licence tier: Starter is fine for development; plan the Indie upgrade before sales.
15. GitHub: which repository should CI run in? The workflow is ready in `.github/workflows/ci.yml`.
16. UI: short color-chip labels ("Mediant", "Neo-Riemann", full names in tooltips). OK?

New from the Build out session:

17. Online AI (decided: offline by default, online opt-in; ADR 0010). Which model and who pays for
    API usage at launch: each user's own key (current build), or a Second Opinion account later?
18. Key change in Build out: a final-chorus key change is skipped when your idea comes after the
    bridge, so your idea stays exact. Allow transposing the idea for the last chorus?
19. The idea section shows one option (your chords as played). Also offer variations of the idea
    itself (for example richer extensions), clearly labelled?
20. Binary size: the prebuilt ONNX Runtime for macOS is 67 MB, above the 60 MB per-format budget.
    The minimal static build (ADR 0009) fixes this before release.
