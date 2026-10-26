# ADR 0009: ONNX Runtime and Basic Pitch note transcription (P1-08)

Status: implemented on Linux (measured); macOS and Windows builds written but not yet run

## Decision

- **Model:** Spotify Basic Pitch `basic_pitch/saved_models/icassp_2022/nmp.onnx` (Apache-2.0),
  committed as `models/basic_pitch/nmp.onnx` (Git LFS) with the upstream `LICENSE` and `NOTICE`.
  Source: github.com/spotify/basic-pitch at commit `fa5997af0a8210982619003269994a1be25eddf3`
  (2025-11-13). SHA-256 `2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec`,
  230,444 bytes.
- **Runtime:** ONNX Runtime **1.21.0** (released 2025-03-07, commit `e0b66cad`), CPU execution
  provider, official prebuilt release for now, behind `SO_ENABLE_ONNX` (default ON).
  1.21.1 has no C/C++ archives on the release page (404), so 1.21.0 is the pinned version for
  both C++ and the Python reference (`tools/ml/requirements.txt`).
- **Code:** `so_ml` static library (`src/core/analysis/BasicPitchRunner.{h,cpp}`, no JUCE) linked
  to `so::onnxruntime`. `so_core` and `so_unit_tests` stay ORT-free. The plugin links `so_ml`
  only when `SO_ENABLE_ONNX` is ON and gets `SO_HAS_ONNX=1`.

## Verified model I/O (onnx 1.23.2 + onnxruntime 1.21.0)

Opset 15 (plus ai.onnx.ml 2), IR 8, produced by tf2onnx 1.15.1. Batch dimension is dynamic.

| Tensor | Name | Shape | Meaning |
|---|---|---|---|
| input | `serving_default_input_2:0` | [batch, 43844, 1] float32 | 22,050 Hz mono, 2 s minus one hop |
| output | `StatefulPartitionedCall:1` | [batch, 172, 88] | note (frame) posteriors |
| output | `StatefulPartitionedCall:2` | [batch, 172, 88] | onset posteriors |
| output | `StatefulPartitionedCall:0` | [batch, 172, 264] | contour (3 bins per semitone), unused |

This matches the PRD. The runner checks the names when it creates the session and asks only for
the note and onset outputs. Operators used: Conv, Reshape, Pad, Transpose, Concat, Slice,
Unsqueeze, Neg, Mul, Cast, Relu, Sigmoid, Add, ReduceSum/Min/Max, Sqrt, Log, Shape, Sub, Div,
Equal, Where (input for the reduced-operator config below).

## Port of the Python reference

- `inference.py`: prepend 3,840 zeros (half of the 30-frame overlap), windows of 43,844 samples
  every 36,164 samples (zero-padded at the end), one window per `Run`, keep frames 15..156 of each
  window, trim to `int(n / 36164 * 142)` frames.
- `note_creation.py output_to_notes_polyphonic`: inferred onsets (frame differences, n_diff 2,
  rescaled to the onset maximum), `argrelmax` peaks over time, onset threshold 0.5, frame
  threshold 0.3, energy tolerance 11, minimum length `round(127.7 ms * 86.13 fps) = 11` frames,
  neighbour suppression, melodia trick (global argmax in numpy row-major order, kept fast with a
  per-row maximum cache), times from `model_frames_to_time` including its per-window offset and
  `MAGIC_ALIGNMENT_OFFSET`. Pitch bends are skipped. Arithmetic is float64 where numpy is.
- `NoteEvent.velocity` is the mean note posterior (Basic Pitch "amplitude"). `confidence` is the
  onset peak for onset-detected notes and the amplitude for melodia-trick notes. Notes are sorted
  by start time, then pitch.
- `NotePosteriorgram` (frames x 88, plus frame times) is returned for chroma (P2-01).
- Input at other rates goes through a small private Kaiser-windowed sinc resampler (24 zero
  crossings, beta 8.6, cutoff 0.95 of the lower Nyquist). Replace it with the shared analysis
  Resampler once that lands. The Python reference uses librosa/soxr, so the fixture is at 22,050 Hz.
- NFR-15: one process-wide `Ort::Env` and one `Ort::Session` per model path, created lazily on the
  first `prepare()`/`run()` and released when the last runner goes away (weak references).
  Session options: 1 to 2 intra-op threads (default 2), 1 inter-op thread, sequential execution,
  all graph optimisations, `session.intra_op.allow_spinning = 0` so idle ORT threads do not burn
  DAW CPU. `run()` blocks; the cancel flag is polled before every window; never call it from the
  audio thread.

## Results (Linux x64, Release, shared 2-core VM)

`so_ml_tests` against `tests/fixtures/basic_pitch_piano.expected.json` (Python
`basic_pitch.inference.predict` with the same ONNX model and onnxruntime 1.21.0):

- 44 of 44 notes matched, pitch exact: precision 1.000, recall 1.000, **F-measure 1.000**;
  **max onset error 0.00 ms**, max offset error 0.00 ms. Acceptance is F >= 0.90 and 20 ms.
- Posteriorgram: 432 frames, sum identical to 4 decimals, max error on 5 sampled rows 5e-6.
- 44.1 kHz input (fixture upsampled, then resampled back by the runner): F 1.000.
- **64 s of audio: 3.0 to 4.9 s** end to end (at 22,050 Hz: 3.5 and 4.3 s; at 44.1 kHz including
  resampling: 4.9 and 3.1 s), 2 intra-op threads, on a VM shared with five other build agents, so
  treat it as an upper bound. NFR-06 (32 bars at 120 BPM = 64 s in 10 s total) has room for the
  rest of the pipeline. Run it with `so_ml_tests "[.timing]"`.

## Linking and bundling per OS (interim, prebuilt)

`cmake/OnnxRuntime.cmake` downloads the release archive at configure time with `file(DOWNLOAD)`,
pinned URL and `EXPECTED_HASH SHA256`, and extracts it into `build/<preset>/_deps/onnxruntime`.
Offline: `-DSO_ORT_ARCHIVE=<archive>` (hash checked) or `-DSO_ORT_ROOT=<extracted dir>`.

| OS | Archive | SHA-256 | Shipped file | Where it goes |
|---|---|---|---|---|
| Linux x64 | `onnxruntime-linux-x64-1.21.0.tgz` | `7485c7e7aac6501b27e353dcbe068e45c61ab51fbaf598d13970dfae669d20bf` | `libonnxruntime.so.1` (20.6 MB) | next to the binary (VST3 `Contents/x86_64-linux/`, Standalone folder), `RPATH=$ORIGIN` |
| macOS universal2 | `onnxruntime-osx-universal2-1.21.0.tgz` | `3c3cfc71e538e592192c14d9bad88ec5d5d8d5d698ebc2c6b3119be8c90b4670` | `libonnxruntime.1.21.0.dylib` (67.4 MB) | `Contents/Frameworks/` of the VST3, AU and Standalone bundles, rpath `@loader_path/../Frameworks` |
| Windows x64 | `onnxruntime-win-x64-1.21.0.zip` | `5c07bb2805cd666dda75fa9bfa60e75f2f90d478b952298dd9d55c00740d81bf` | `onnxruntime.dll` (13.0 MB) | next to the binary (VST3 `Contents/x86_64-win/`, Standalone folder) |

The hashes were computed from the GitHub release assets downloaded on 2026-10-26 (the GitHub API
was not reachable from the build machine to cross-check published digests); recheck them once
from a normal network.

The model, its LICENSE and NOTICE go to `Resources/basic_pitch/` (VST3 and macOS bundles:
`Contents/Resources/basic_pitch/`; Windows and Linux Standalone: `<exe dir>/Resources/basic_pitch/`),
and ORT's LICENSE and ThirdPartyNotices to `Resources/licenses/onnxruntime/`. The plugin code that
resolves the model path from the bundle (AnalysisService, P2-04) is not part of this ticket.

- macOS: the files are added as bundle sources (`MACOSX_PACKAGE_LOCATION`), so they are in the
  bundle before JUCE's copy-after-build and before code signing. Release signing must sign
  `Contents/Frameworks/*.dylib` too.
- Windows and Linux: post-build copies. JUCE's own copy-after-build step runs before them, so keep
  `SO_COPY_PLUGIN` OFF there (the presets already do) or the installed copy lacks the runtime.

Verified on Linux (Release build of `SecondOpinion_VST3` and `SecondOpinion_Standalone`):
`libonnxruntime.so.1` sits next to `Second Opinion.so` and the Standalone binary, both have
`RUNPATH=$ORIGIN`, and `Contents/Resources/basic_pitch/` plus `licenses/onnxruntime/` are in the
VST3 bundle. Until plugin code calls the runner (P2-04), the linker drops the unused `so_ml`
objects, so the binaries do not yet list `libonnxruntime.so.1` as NEEDED.

## DLL clash mitigation

Other plugins in the same host may load their own, different ONNX Runtime.

- **Windows** (highest risk: Windows 11 also ships an `onnxruntime.dll` in System32, and a plugin
  DLL's folder is not on the default search path). `so_ml` is compiled with
  `ORT_API_MANUAL_INIT`, so the only import is `OrtGetApiBase`, and it is delay-loaded
  (`/DELAYLOAD:onnxruntime.dll`). The runner finds the folder of the module that contains it
  (`GetModuleHandleExW` from an address), loads `onnxruntime.dll` from there with
  `LoadLibraryExW(full path, LOAD_WITH_ALTERED_SEARCH_PATH)` and initialises the API from that
  handle's `OrtGetApiBase`. A full-path load never binds to a same-named DLL that is already loaded
  or sits in System32. `Config::runtimeLibraryPath` can override the path. The delay-loaded import
  is only a fallback, so a missing DLL never stops the plugin from loading.
- **macOS:** two-level namespaces plus our own copy under `@rpath` in the bundle. Another
  plugin's dylib with the same install name loads from its own path.
- **Linux:** the loader reuses an already-loaded library with the same SONAME
  (`libonnxruntime.so.1`), so another plugin's ORT could be picked up. Acceptable for the dev/CI
  target; `runtimeLibraryPath` (dlopen with `RTLD_LOCAL`) is available, and the static build
  removes the problem.
- The ORT handle is never unloaded (unloading with live thread pools is unsafe); sessions and the
  env are released when the last runner is destroyed, before plugin unload.

## Plan: minimal static reduced-operator build (before beta)

The prebuilt libraries miss NFR-09 on macOS (67 MB universal before our code). Replace them behind
the same `so::onnxruntime` target, as basicpitch.cpp does:

1. Convert the model to ORT format and generate the operator config:
   `python -m onnxruntime.tools.convert_onnx_models_to_ort models/basic_pitch/nmp.onnx`
   (writes `nmp.ort` and `nmp.required_operators.config`).
2. Build ORT 1.21.0 from source per platform (macOS arm64 and x86_64, lipo'd; Windows x64) with
   `--minimal_build --include_ops_by_config nmp.required_operators.config --disable_ml_ops
   --disable_rtti --skip_tests --cmake_extra_defines CMAKE_POSITION_INDEPENDENT_CODE=ON`
   (no `--build_shared_lib`, so static archives) and link those into the plugin. Keep exceptions
   on: the runner catches `Ort::Exception`.
3. Hide ORT symbols: `-fvisibility=hidden` and an exported-symbols list or version script that
   exports only the plugin entry points (macOS `-exported_symbols_list`, Linux version script),
   so two plugins with statically linked ORT cannot interpose each other. On Windows static
   linking alone avoids the DLL search problem.
4. Ship `nmp.ort` instead of `nmp.onnx` (a minimal build only loads ORT format).
5. CI: size budget check per format (NFR-09 < 60 MB), and the fixture test against the static build.

Expected result (estimate): a few MB of ORT code per architecture instead of 20 to 67 MB.

## Git LFS

`.gitattributes` routes `*.onnx` and `tests/fixtures/*.wav` to Git LFS; git-lfs 3.4.1 is installed
here and both files are committed as LFS objects. A clone without LFS gets pointer files: the
runner reports "is a Git LFS pointer; run `git lfs pull`" and the fixture test fails on the WAV
header. CI checkouts need `lfs: true`.

## Not verified yet

- macOS and Windows configure/build, bundle layout, delay-load path and signing (no machines here).
- Multi-instance stress in a DAW (NFR-15: 10 instances).
