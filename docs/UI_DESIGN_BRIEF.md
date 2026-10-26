# UI design brief for Claude Design: "Second Opinion"

## How to use this (for Lawrence)
- **Which tool:** **Claude Design**, Anthropic's design tool. As of October 2026 it lives inside Claude: you can open it from any Claude conversation, from the **Artifacts** tab ("Design"), or from **Claude Code** with the `/design` command. The standalone site (claude.ai/design) closes on December 14, 2026 ([Claude Design](https://claude.com/product/design), [Help Center](https://support.claude.com/en/articles/17440474-migrate-from-standalone-claude-design-to-claude)).
- **Steps:**
  1. Start a Claude conversation (or run `/design` in Claude Code inside the plugin repo) and paste the **prompt below the line**.
  2. Let it build the **Main workspace** first, then ask for the other screens and states one at a time. Anthropic's tips: start simple and layer complexity, give specific feedback (e.g., "tighten card spacing to 8px"), ask for 2–3 variations, and ask Claude to review contrast and accessibility ([Get started guide](https://support.claude.com/en/articles/14604416-get-started-with-claude-design)).
  3. Use inline comments on elements for small fixes and chat for big layout changes.
  4. Before handoff, ask it to show **empty, loading, error and low-confidence states** (Anthropic recommends flagging edge cases before handoff).
  5. Hand off to Claude Code, or export as **HTML**. Claude Code then implements it in `ui/` (the WebView frontend). Once the repo has real components, `/design-sync` in Claude Code can pull them back into Claude Design so future iterations use the real ones.
- **Usage:** design work counts toward your normal Claude plan limits, and big designs use more.

---

## PROMPT (paste everything below into Claude Design)

**Goal.** Design the complete UI for **Second Opinion**, a professional audio plugin (it runs inside DAWs like Ableton Live, Logic Pro and FL Studio as a resizable window). It listens to audio (a vocal, topline, sample or bounce), shows the chords it hears with a confidence level for each, and offers reharmonization suggestions with voice-leading, bass motion and a tension control. It explains *why* each suggestion works, and the user drags the result into the DAW as MIDI. It runs entirely offline.

**Audience.** Working music producers and topliners (often not trained keyboard players) plus theory-literate keys players. They work late, in dark studios, fast. They hate bloated, confusing plugin UIs (a competitor lost users with a sprawling multi-page layout). **One main window, no page-hopping.**

**Output format.** Build it as an interactive HTML/CSS/TypeScript prototype with mock data. Use CSS custom properties for every token below. No external network resources (no CDN fonts, images or scripts): it will be embedded in a plugin and must work offline, so assume fonts are bundled locally. Name components clearly (the names below), because an engineer will implement these exact components.

### Window and layout
- Default size **1100 × 700 px**; **resizable** between **860 × 560 (min)** and **1800 × 1100 (max)**; UI scale setting 75–200%. Show the layout at 1100×700 and at 860×560, and say how it reflows (the right panel collapses into a drawer at the minimum size).
- Regions (top to bottom):
  1. **HeaderBar** (44 px): logo wordmark "Second Opinion"; source pill (Captured 32 bars · 92 BPM · F minor) with key override dropdown; Capture button (record dot) / Stop; "Drop audio" hint; Settings gear; Undo/Redo.
  2. **TimelinePanel** (~200 px, full width): bar ruler (1–32, scrollable/zoomable), **OriginalLane** (detected chords), **SuggestionLane** (selected suggestion, aligned under the original), **RomanRow** (Roman numerals + function color tick), a thin **MelodyStrip** (note blips of the detected melody), and a playhead.
  3. **Workspace** (flex): left = **SuggestionGrid** (cards); right (320 px) = **WhyPanel**.
  4. **ControlRail** (~120 px): Tension knob, Color chips, Bass motion segmented control, Smoothness knob, Range control, Density stepper, Melody-on-top toggle, Generate / Shuffle buttons.
  5. **DragStrip** (48 px, bottom): drag handles **Chords · Bass · Top line · All** + "Export MIDI…" + preview level. *(This strip is implemented as a native component in the plugin. Design it visually like the rest, but keep it as a separate full-width bar at the very bottom.)*

### Components (design every one, with states)
1. **ChordSegment** (in OriginalLane/SuggestionLane). Rounded block whose width equals its duration. Chord symbol in the chord typeface (e.g., `Fm9`, `D♭maj7/F`, `G/B`), with true ♭ ♯ glyphs. **Confidence shading:** fill opacity plus a diagonal hatch for low confidence (< 0.5), with a small confidence dot (●●● / ●●○ / ●○○). Never color-only. States: default, hover (shows confidence %), selected, **user-locked** (lock icon + solid border), **user-corrected** (small pencil mark), low-confidence (hatched + "?"), dragging edge (merge/split handles).
2. **ChordPopover** (opens on a segment click): top 3 alternative readings, each with confidence bar and the notes shown on a mini keyboard; a text field "Type a chord (e.g., G/B)" with validation; Lock toggle; Split here / Merge with next.
3. **SuggestionCard** (grid of 6–12). Title (auto, e.g., "Borrowed ♭VI lift"), a mini chord strip of the progression, technique tags (Diatonic, Borrowed, Secondary, Tritone sub, Chromatic mediant, Neo-Riemannian), a **tension meter** (0–100), a voice-movement indicator ("smooth"), a star (favorite) and a play button. States: default, hover, selected, playing (animated playhead within the mini strip), favorited, and "contains locked bars" badge. Keyboard focus ring.
4. **WhyPanel**: for the selected card or chord. Plain-language sentence first, theory term in parentheses (e.g., "This chord borrows from F minor's parallel major to brighten the turn (modal interchange, ♭VI → IV)"). Shows function chip (Tonic / Predominant / Dominant), Roman numeral large, "Melody note C is the 9th here," and a voice-leading mini diagram (voices as lines moving between two chord stacks). Toggle **Simple / Detailed**. Empty state: "Select a chord or suggestion to see why it works."
5. **TensionKnob**: large (72 px) rotary knob 0–100 with a cool→warm arc gradient, value readout, labels "Safe · Colorful · Spicy" at 0/50/100. States: idle, hover, dragging (fine-adjust with Shift), automated (small "A" badge when the host is automating), disabled.
6. **SmoothnessKnob**: medium knob, 0–100, label "Voice-leading: leapy ↔ smooth."
7. **ColorChips**: multi-select chips for the 6 technique families, with a tiny tooltip explanation each.
8. **BassMotionControl**: segmented control (Root · Inversions · Walking · Pedal). Pedal reveals a note picker.
9. **RangeControl**: a compact piano strip (C1–C7) with two draggable handles for chord range and a separate range for bass; Density stepper 3–6 notes.
10. **MelodyOnTopToggle**: switch + small option "exact / 3rd below / 6th below."
11. **Generate / Shuffle** buttons: primary and secondary. Generate shows a spinner state.
12. **DragHandle** (×4 in DragStrip): a grip icon + label + tiny lane preview. States: idle, hover ("Drag into your DAW"), dragging (ghost), disabled (no suggestion yet).
13. **AnalysisProgress** overlay/state: stages **Preparing → Transcribing notes → Reading harmony → Writing suggestions** with a progress bar per stage, elapsed time, and Cancel. Make it calm and confident, not flashy.
14. **CaptureState**: armed (pulsing record dot, "Press play in your DAW"), recording (bar counter 1…32 with live input meter), stopped.
15. **Toasts/Errors**: inline, human messages (e.g., "Couldn't find a steady pitch, so try soloing the vocal." with a Retry button). Also "No tempo from host: set BPM" with a number field.

### Screens and states to deliver
1. **Empty / first run**: big drop zone "Drop a vocal, loop or bounce" + "or Capture from this track" button; a 3-step hint (Capture → Pick a suggestion → Drag to DAW).
2. **Capturing** (armed and recording).
3. **Analyzing** (progress state).
4. **Main workspace**: populated with mock data (32 bars in F minor, e.g., original `Fm9 | D♭maj7 | A♭/C | E♭sus4 E♭ |…`, 8 suggestion cards).
5. **Chord popover open** on a low-confidence chord.
6. **Low-confidence analysis** (several hatched segments + banner "Some chords are uncertain. Click to confirm.").
7. **Error state** (analysis failed) and **No-tempo** prompt.
8. **Settings screen** (modal sheet over the workspace, tabs): *Notation* (Pop `Cmaj7` / Jazz `CΔ7`; prefer slash chords; ♭/♯ spelling auto/flats/sharps), *Sound* (preview sound: Keys / Pad / Off; level), *MIDI export* (PPQ 480/960; velocity: flat/humanized; lane naming), *Analysis* (quality Fast/Balanced/Accurate; source hint default; clear cache), *Interface* (UI scale 75–200%; reduce motion), *About* (version, credits, third-party licenses list, "Works fully offline").
9. **Compact layout** at 860×560 (WhyPanel as a slide-in drawer).

### Visual style
- **Dark, pro-audio, calm.** Feels like a high-end studio tool, not a toy or a "gamer" UI. Think late-night studio, warm brass accents on graphite, with precise, instrument-like detail. Avoid generic AI-looking choices: no purple gradients, no glassmorphism overload, no default Inter/Roboto look. Dominant neutrals with one sharp accent.
- Subtle depth: 1 px hairline borders, soft inner shadows on wells, a faint noise/grain texture on the main background (very subtle, under 3% opacity).
- Motion: minimal and purposeful (card hover lift of 2 px; 120 ms ease-out; progress bar shimmer). Respect a "reduce motion" setting.

### Tokens (use exactly these as CSS variables)
```
--so-bg:            #0E0F12;   /* window background */
--so-surface-1:     #15171C;   /* panels */
--so-surface-2:     #1C1F26;   /* cards, wells */
--so-surface-3:     #252934;   /* hover, raised */
--so-border:        #2E3340;
--so-border-strong: #3D4352;
--so-text:          #ECEDEF;   /* primary text */
--so-text-2:        #A6ACB8;   /* secondary */
--so-text-3:        #6E7480;   /* muted, captions */
--so-accent:        #E3A44A;   /* brass: primary actions, selection */
--so-accent-hover:  #F0B65E;
--so-accent-ink:    #1A1307;   /* text on accent */
--so-original:      #57C7B8;   /* detected/original lane */
--so-suggestion:    #E3A44A;   /* suggestion lane (accent) */
--so-conf-high:     #57C7B8;
--so-conf-mid:      #D9C25B;
--so-conf-low:      #E07A5F;   /* always paired with hatch + "?" */
--so-fn-tonic:      #6FA8FF;
--so-fn-predominant:#B48CFF;
--so-fn-dominant:   #FF8A5C;
--so-tension-0:     #5B8CFF;   /* cool */
--so-tension-100:   #FF5E3A;   /* warm */
--so-danger:        #E5484D;
--so-success:       #46A758;
--so-focus:         #F0B65E;   /* 2px focus ring */
--so-radius-sm: 4px; --so-radius-md: 8px; --so-radius-lg: 12px;
--so-space-1: 4px; --so-space-2: 8px; --so-space-3: 12px; --so-space-4: 16px; --so-space-6: 24px;
```
Check every text/background pair for **≥ 4.5:1 contrast** (≥ 3:1 for large text and UI glyphs) and adjust the tokens if needed. Report any changes.

### Typography (bundled locally, open-licensed)
- UI: **IBM Plex Sans** (400/500/600). Numbers and values: **IBM Plex Mono** (tabular). Chord symbols: **IBM Plex Sans Condensed SemiBold**, with superscript-style extensions (e.g., `F m⁹`, `D♭ maj⁷/F`) and real ♭ ♯ glyphs.
- Scale: 11 (captions), 12 (labels), 13 (body), 15 (card titles), 20 (chord symbols in the timeline), 32 (Roman numeral in the WhyPanel).

### Interaction notes
- Click a card → it fills the SuggestionLane and the WhyPanel. Space = audition; ←/→ = previous/next card; L = lock the selected bars; R = regenerate unlocked bars.
- Dragging across the bar ruler selects a range. Lock and Regenerate act on the selection.
- A/B toggle near the play button: "Original ↔ Suggestion."
- The Tension knob shows a live preview of the average tension of the current set (a small sparkline).

### Implementation note (for the engineer reading this design)
This UI will run in a JUCE `WebBrowserComponent` (WebView) inside the plugin, so build it with plain HTML/CSS/TypeScript components (framework-light; Preact is acceptable). It needs:
- **no external URLs**;
- all state coming from a `bridge` module, so that you **mock the bridge** with realistic JSON (AnalysisResult, Suggestion[], progress events);
- every knob and toggle written as a component that takes `value`, `onChange` and an `automated` flag, so it can bind to host parameters;
- the DragStrip kept as a separate bottom bar, as it will be replaced by a native component.

Please deliver: the screens above, a component sheet (every component and state), and the token sheet. After the first pass, review your own design for accessibility, contrast and information hierarchy, then propose 2 alternative directions for the Main workspace (e.g., cards as a horizontal strip vs. a grid).

---

## Appendix A: Translating the design into JUCE (recommendation)

**Recommendation: use a WebView UI (JUCE 8+/9 `WebBrowserComponent` with native integration) for the main editor, plus a thin native JUCE strip for MIDI drag.**

Why:
1. Claude Design produces HTML/CSS/TS, and Claude Code can implement it directly in `ui/`. That avoids a lossy redesign into `LookAndFeel` code, and the `/design-sync` round-trip keeps design and code aligned.
2. JUCE 8 added first-class WebView support: native functions, events, a resource provider for embedded assets, and parameter relays for binding ([JUCE 8 WebView overview](https://juce.com/blog/juce-8-feature-overview-webview-uis/)). JUCE 9.0.1 publishes the interop library as a TypeScript npm package, `@juce-framework/webview` ([JUCE 9.0.1](https://forum.juce.com/t/juce-9-0-1-is-out/69269)).
3. Complex, text-heavy UI (cards, popovers, explanations, settings) is faster to build and polish in HTML/CSS.

Engineering rules for the WebView approach:
- **One WebView per plugin instance.** Multiple WebViews per editor are costly and quirky ([JUCE forum](https://forum.juce.com/t/i-want-advice-on-combining-juce-with-web-technologies/66868)).
- **Windows:** use the WebView2 backend and set a user-data folder (required for plugins). Consider `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING`, and set `NEEDS_WEBVIEW2` in CMake ([JUCE docs](https://docs.juce.com/develop/classjuce_1_1WebBrowserComponent.html)). Show a friendly message if the WebView2 runtime is missing.
- **Assets** are embedded via `juce_add_binary_data` and served by `withResourceProvider`, with no network.
- **Data flow:** native functions and events for normal control and JSON data; use the resource provider for bulk or binary data (e.g., waveform or note arrays) instead of large JSON events ([JUCE forum](https://forum.juce.com/t/the-new-webbrowsercomponent-and-valuetrees/61061)).
- **Parameters:** bind knobs and toggles through JUCE's WebView relays plus parameter attachments, so host automation works.
- **Drag-to-DAW:** a native `juce::Component` docked below the WebView calls `performExternalDragDropOfFiles` (PRD P1-05 spike).
- **Keyboard focus:** test Space and arrow-key handling in each DAW, because hosts sometimes eat key events. Also provide clickable equivalents.

**When to prefer native `LookAndFeel` instead:** if WebView proves unstable in a key host during the P1-04/P1-05 spikes. In that case, map the tokens above to a custom `LookAndFeel_V4` subclass (`ColourIds` from the token table, `drawRotarySlider` for the Tension and Smoothness knobs, custom `Component`s for ChordSegment, SuggestionCard and WhyPanel), and embed the IBM Plex fonts via BinaryData (JUCE 9 also supports variable fonts). Keep the component names identical so the design still maps 1:1.
