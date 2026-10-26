// Undo/redo snapshots (FR-ST-02) and what they mean for the native session (restoreSnapshot).

import type { SnapshotPayload } from '../bridge/contract';
import type { State } from './store';

export type Snap = Pick<State, 'analysis' | 'locked' | 'suggestions' | 'generation' | 'selectedSuggestionId' | 'song'>;

/** Chord symbols cross the bridge in ASCII (Ab, F#m7b5): some WebView backends mangle non-ASCII
    text on the way to C++, and the C++ parser reads both spellings. */
export function asciiSymbol(symbol: string) {
  return symbol.replace(/♭/g, 'b').replace(/♯/g, '#').replace(/𝄫/g, 'bb').replace(/𝄪/g, '##');
}

/** The native state a snapshot stands for: chords and locks, the suggestion set, the song. */
export function snapshotPayload(after: Snap): SnapshotPayload {
  return {
    segments: after.analysis?.segments.map((seg) => ({
      id: seg.id,
      symbol: asciiSymbol(seg.symbol),
      confidence: seg.confidence,
      userLocked: after.locked.has(seg.id),
      userCorrected: !!seg.userCorrected,
    })),
    generation: after.generation,
    song: after.song ? { generation: after.song.generation, chosen: after.song.sections.map((x) => x.chosen) } : null,
  };
}
