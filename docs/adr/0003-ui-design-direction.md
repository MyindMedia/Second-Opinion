# ADR 0003: UI design direction and token changes

Status: accepted (Phase 1)

## Direction

Graphite and brass, instrument-precise, calm. One accent (brass) for actions and suggestions; teal is
reserved for "what we heard". The memorable element is the two-lane timeline: the suggestion lane
fills only the chords that changed in brass and leaves unchanged chords as quiet outlines, so you see
where a reharmonization differs at a glance. Everything else stays quiet: hairline borders, sentence
case labels, no gradients except the Tension arc (cool to warm, as the brief asks).

Type follows the brief: IBM Plex Sans for UI, IBM Plex Mono for values, IBM Plex Sans Condensed
SemiBold for chord symbols, with extensions raised (F m⁹, D♭ maj⁷/F).

## Changes to the brief's tokens (reported, as the brief asked)

| Token | Brief | Now | Why |
|---|---|---|---|
| `--so-text-3` | #6E7480 | #7C828E | #6E7480 measured 4.1:1 on bg and 3.5:1 on surface-2, below 4.5:1 for small text. #7C828E measures 5.0:1 on bg and 4.6:1 on surface-1. Small text on surface-2/3 uses text-2 (7.2:1 / 6.4:1). |

All other tokens are unchanged. Measured ratios are listed at the top of `ui/src/styles/tokens.css`.

## Glyphs

IBM Plex has no ♭ ♯ glyphs. A 2.8 KB subset of Noto Music (SIL OFL 1.1) is bundled as
"SO Accidentals" with a `unicode-range` of U+266D-266F and U+1D12A-1D12B, so true accidentals render
offline in every font stack.

## Layout decisions

- Default timeline zoom shows about 12 bars at 1100 px; Ctrl/Cmd + wheel or the +/- buttons zoom.
- Suggestion cards show the 4 bars around their first change (labelled with the bar number) so chord
  symbols stay readable; the full 32 bars are in the suggestion lane.
- Color chips use short labels (Mediant, Neo-Riemann) with full names in tooltips, so the control
  rail fits at 1100 px.
- Compact layout below 1000 × 640 (window size divided by UI scale): the WhyPanel becomes a drawer
  opened from the header, the rail tightens, and captions hide.
