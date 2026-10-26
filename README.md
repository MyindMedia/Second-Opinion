# Second Opinion

An audio-aware reharmonizer plugin (VST3, AU, Standalone; AAX later). It hears the harmony in a
vocal, topline, sample or bounce, shows how sure it is, offers producer-grade reharmonizations with
real voice-leading, explains why each one works, and lets you drag the result into your DAW as MIDI.
Everything runs offline.

Built with C++20, JUCE 9.0.4 and CMake. The interface is a WebView UI (TypeScript + Vite) embedded in
the plugin binary; the MIDI drag strip at the bottom is a native JUCE component.

> Status: listening, reharmonization and Build out (song builder) work end to end. See `STATUS.md`
> for measured results and known limits.

Two modes in one window:

- **Reharm**: capture or open an idea, see the chords it hears (with confidence), and audition
  reharmonizations that explain why they work.
- **Build out**: describe the song ("dark late-night R&B, hopeful chorus"), pick moods, and get a
  full song built around your idea, three options per section, ready to drag into your DAW.

## Build on macOS (universal: Apple Silicon + Intel)

You need:

1. Xcode 15 or newer, opened once so it installs its command line tools.
2. CMake 3.25 or newer: `brew install cmake`
3. Node.js 20 or newer (optional): `brew install node`. Without Node, the build embeds the prebuilt
   UI in `ui/dist`.

Then, from this folder:

```sh
cmake --preset mac-universal-debug
cmake --build --preset mac-universal-debug
ctest --preset mac-universal-debug
```

The first configure downloads JUCE 9.0.4, Catch2 and ONNX Runtime (about 2 to 5 minutes, internet
needed once). The mac presets copy the
built plugins into `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`, so your
DAW finds them after a rescan. The Standalone app is in
`build/mac-universal-debug/SecondOpinion_artefacts/Debug/Standalone/`.

Use `mac-universal-release` for an optimized build.

Validate (optional, recommended):

```sh
# pluginval: https://github.com/Tracktion/pluginval/releases
pluginval --strictness-level 5 --validate "build/mac-universal-release/SecondOpinion_artefacts/Release/VST3/Second Opinion.vst3"
auval -v aufx SOp1 Brmt
```

## Build on Windows 10/11 (x64)

You need Visual Studio 2022 (Desktop development with C++), CMake 3.25+, and Node.js 20+ (optional).
With Visual Studio 2026 instead, add `-G "Visual Studio 18 2026"` to the configure command (CMake 4.2+).
The WebView2 SDK is linked statically, so install the NuGet package once in PowerShell:

```powershell
$ver  = '1.0.3485.44'
$dest = "$env:USERPROFILE\AppData\Local\PackageManagement\NuGet\Packages\Microsoft.Web.WebView2.$ver"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$ver" -OutFile "$env:TEMP\webview2.zip"
Expand-Archive "$env:TEMP\webview2.zip" -DestinationPath $dest -Force
```

Then:

```powershell
cmake --preset win-x64-debug
cmake --build --preset win-x64-debug
ctest --preset win-x64-debug
```

To skip static WebView2 linking, configure with `-DSO_WEBVIEW2_STATIC=OFF`.

## Linux (development and CI only)

```sh
sudo apt-get install ninja-build libwebkit2gtk-4.1-dev libgtk-3-dev libasound2-dev libcurl4-openssl-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

## Working on the UI

```sh
cd ui
npm ci
npm run dev        # http://localhost:3000, with a mocked bridge and example data
npm test           # Vitest
npm run typecheck
```

Preview states with a query string: `?state=ready`, `empty`, `popover`, `analyzing`, `armed`,
`recording`, `error`, `tempo`, `settings&tab=About`. Add `&automate` to simulate host automation on
the Tension knob (shows the "A" badge).

Inside the plugin, the UI is served from BinaryData, never from the network. `cmake --build` rebuilds
the UI automatically when anything in `ui/src` changes.

## Repository layout

```
src/core/theory    Pitch, Interval, ChordQuality, Chord, ChordSymbolParser, ChordNamer, Key, RomanNumeral
src/core/midi      MidiWriter (SMF type 1, 960 PPQ), SimpleVoicer (placeholder until P3-01)
src/plugin         PluginProcessor (zero-latency pass-through), PluginEditor (WebView), Parameters
src/bridge         WebBridge (native functions), NativeDragStrip (MIDI drag to DAW)
ui/                Vite + TypeScript WebView frontend (tokens in ui/src/styles/tokens.css)
tests/unit         Catch2 tests for the headless core
tests/rt           Real-time safety (allocation trap) and state robustness tests
docs/              PRD, UI brief, ADRs, licenses, open questions
```

`src/core` has no JUCE dependency, so it builds and tests headless.

## Rules this codebase follows

- No allocation, locks, I/O or logging in `processBlock` (checked by `tests/rt`).
- No network access anywhere in the product (CI fails if the built UI contains external URLs).
- Any randomness takes an explicit seed.
- Parameter IDs in `src/plugin/Parameters.h` are part of saved projects. Never rename them.
