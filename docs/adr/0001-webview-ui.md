# ADR 0001: WebView UI with a native drag strip

Status: accepted (Phase 1)

## Context

The UI brief asks for a single, text-heavy window (cards, popovers, explanations, settings) that a
design tool can iterate on in HTML/CSS. JUCE 8+ ships first-class WebView support, and JUCE 9.0.1
publishes the interop library as the npm package `@juce-framework/webview`.

## Decision

- The whole editor is one `juce::WebBrowserComponent` per plugin instance, serving a Vite build of
  `ui/` from BinaryData through `withResourceProvider`. No network access.
- `ui/dist` is zipped at build time (`cmake/ZipDir.cmake`) and embedded as one BinaryData entry. This
  keeps file names stable for the resource provider and avoids configure-time globbing.
- Every automatable parameter is bound with JUCE relays (`WebSliderRelay`, `WebToggleButtonRelay`,
  `WebComboBoxRelay`) and parameter attachments, so host automation and the UI stay in sync.
- The UI tells user gestures apart from host automation by the echo window: a value that arrives within
  400 ms of our own write is our echo; anything else is the host, which shows the "A" badge.
- The WebView2 backend is selected on Windows only. On macOS (WKWebView) and Linux (WebKitGTK) the
  platform default is used. Selecting `webview2` elsewhere makes `areOptionsSupported` fail; this was
  caught by running the Linux Standalone build and is guarded by `#if JUCE_WINDOWS`.
- The MIDI drag strip is a native `juce::Component` docked under the WebView (see ADR 0002).

## Consequences

- One codebase for design and implementation; the browser preview (`npm run dev`) runs the real UI
  against a mocked bridge.
- Windows needs the WebView2 runtime (present on Windows 10/11 with current updates). The editor shows
  a plain message if it is missing.
- If WebView proves unstable in a key host, the fallback is a `LookAndFeel_V4` port using the same
  token names (UI brief, Appendix A).
