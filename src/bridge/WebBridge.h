#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class SecondOpinionProcessor;

/** Native functions the WebView UI can call (PRD §7.9). Phase 1 exposes the subset the UI stub needs;
    analysis/generation functions land with their services in Phase 2-4.

      parseChord(symbol)        → { ok, symbol, pop, jazz, notes[], bass, error }   (FR-AN-03 typed correction)
      setProgression(payload)   → { ok, count }    payload = { id, title, bpm, chords:[{symbol, beats}] }
      getSession()              → { stateVersion, ui:{...} }                         (FR-ST-01)
      setSessionValue(key, val) → true
      exportMidi()              → opens a folder chooser and writes the 4 lanes      (FR-OUT-02)
      setSettings(settings)     → Ack   analysis quality/source, preview sound, MIDI PPQ/velocity (FR-SET)
      clearCache()              → { ok, removed }   deletes the captured-audio cache   (FR-ST-01 AC3)
      restoreSnapshot(snapshot) → Ack   undo/redo: segments, generation, song         (FR-ST-02)

    Events (C++ → JS): "transport" { playing, bpm, ppq, numerator, denominator, hasTempo } */
namespace so::bridge
{

juce::WebBrowserComponent::Options addNativeFunctions (juce::WebBrowserComponent::Options options,
                                                        SecondOpinionProcessor& processor,
                                                        std::function<void()> exportMidi);

} // namespace so::bridge
