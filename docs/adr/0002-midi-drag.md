# ADR 0002: MIDI drag to the DAW (spike P1-05)

Status: implemented, host matrix pending

## Decision

`NativeDragStrip` (src/bridge) is a 48 px native component under the WebView with four handles:
Chords, Bass, Top line, All. Dragging a handle:

1. voices the current progression with `so::midi::voiceProgression` (placeholder until P3-01),
2. writes `SecondOpinion_Session_<suggestion>_<lane>.mid` (SMF type 1, 960 PPQ, tempo and time
   signature, notes starting at bar 1) to `<temp>/SecondOpinion/drag/<uuid>/`,
3. calls `DragAndDropContainer::performExternalDragDropOfFiles`.

"Export MIDI..." writes the same four files to a folder the user picks. Drag folders older than one
hour are removed at startup (newer ones may belong to another instance mid-drag).

The UI sends the selected suggestion to C++ with the `setProgression` native function. C++ re-parses
every symbol with the theory core, so the MIDI always comes from the C++ source of truth.

## Verified so far (measured)

- Linux Standalone: selecting a suggestion enables all four handles and their lane previews render
  from the C++ voicing (screenshot in the Phase 1 session).
- MIDI bytes: golden-file and determinism tests in `tests/unit/midi_writer_test.cpp`.

## Still to test (needs real DAWs on macOS and Windows)

| Host | Drag Chords | Drag All | Lands at bar 1 | Notes |
|---|---|---|---|---|
| Ableton Live 12 | | | | |
| Logic Pro 12 | | | | |
| FL Studio | | | | |
| Studio One | | | | |
| Reaper | | | | |
| Cubase | | | | |

If any host refuses the drag, Export MIDI is the fallback, and the next step is testing a
`performExternalDragDropOfFiles` call started from `mouseDown` rather than `mouseDrag` in that host.
