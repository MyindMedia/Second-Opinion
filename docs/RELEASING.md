# Releasing Second Opinion

How builds become installers. Covers PRD ticket P6-04 (signing, notarization, installers). The
workflows are written and linted, but none of this has run on a real Mac or Windows machine yet;
treat the first tagged release as a dry run.

## Workflows

| Workflow | Trigger | What it does |
|---|---|---|
| `.github/workflows/ci.yml` | push, PR, manual | UI checks; Debug + Release build, ctest, pluginval 5 on macOS (universal), Windows (x64) and Linux; auval on macOS; uploads plugin folders |
| `.github/workflows/nightly.yml` | daily 07:17 UTC, manual | pluginval strictness 10 and `auval -strict` (macOS, Windows); ASan + UBSan Debug build of `so_unit_tests` (macOS arm64 and Linux clang 18) |
| `.github/workflows/release.yml` | tag `v*`, manual | Release build on both OSes, `.pkg` and Inno Setup installer, size report, artifacts, draft GitHub release (tags only) |

The Basic Pitch model and the test WAVs are ordinary git files (about 14 MB in total; Git LFS was
dropped because the build environment cannot upload LFS objects). All build jobs cache `.cpm-cache`, which also holds the
ONNX Runtime archive (`SO_ORT_DOWNLOAD_DIR`).

## Cutting a release

1. Set the version in `CMakeLists.txt` (`project(SecondOpinion VERSION x.y.z)`). The plugin
   reports this version; the workflow warns if the tag differs.
2. Commit, then tag and push: `git tag v0.2.0 && git push origin v0.2.0`.
3. Wait for the Release workflow. It creates a **draft** GitHub release with
   `Second Opinion 0.2.0.pkg` and `Second Opinion 0.2.0 Setup.exe`.
4. Check the logs for "unsigned" or "Notarization skipped", install both packages on clean
   machines, open the plugin in at least one DAW per OS, then publish the draft.

Without secrets everything still builds, and the packages are unsigned. Unsigned macOS packages
need right-click > Open (and on macOS 15, System Settings > Privacy & Security); unsigned Windows
installers show a SmartScreen warning. Fine for internal testing, not for customers.

## Secrets (repository settings > Secrets and variables > Actions)

Never commit any of these. The scripts read them only from the environment.

### macOS

| Secret | Value |
|---|---|
| `APPLE_CERTIFICATES_P12` | base64 of one `.p12` holding both Developer ID certificates and private keys (`base64 -i certs.p12 \| pbcopy`) |
| `APPLE_CERTIFICATES_PASSWORD` | password of that `.p12` |
| `APPLE_DEVELOPER_ID_APPLICATION` | `Developer ID Application: <Name> (<TEAMID>)` |
| `APPLE_DEVELOPER_ID_INSTALLER` | `Developer ID Installer: <Name> (<TEAMID>)` |
| `APPLE_TEAM_ID` | the 10-character team ID (checked against the signed bundles) |
| `APPLE_NOTARY_KEY` | contents of the App Store Connect API key (`AuthKey_XXXX.p8`) |
| `APPLE_NOTARY_KEY_ID` | that key's ID |
| `APPLE_NOTARY_ISSUER` | the issuer UUID shown on the API keys page |

Create the API key in App Store Connect > Users and Access > Integrations > Team Keys, with the
Developer role.

### Windows

| Secret or variable | Value |
|---|---|
| `WIN_SIGN_PFX_BASE64` (secret) | base64 of a `.pfx` code-signing certificate |
| `WIN_SIGN_PFX_PASSWORD` (secret) | its password |
| `WIN_SIGN_TIMESTAMP_URL` (variable, optional) | RFC 3161 server, default `http://timestamp.digicert.com` |

Code-signing certificates issued since June 2023 live on a hardware token or a cloud HSM, so a
`.pfx` export may not be possible. In that case run `build_installer.ps1` on a machine that has
the token, with `WIN_SIGN_CERT_SHA1=<thumbprint>`, or swap the `signtool` call for the vendor's
cloud signing tool (for example Azure Trusted Signing). An EV certificate avoids the SmartScreen
reputation warm-up period.

## What the packages contain

**macOS** (`packaging/macos/build_pkg.sh`): one product archive with three optional choices.

| Choice | Installs to |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3/Second Opinion.vst3` |
| AU | `/Library/Audio/Plug-Ins/Components/Second Opinion.component` |
| Standalone | `/Applications/Second Opinion.app` |

Signing order is inside out: `Contents/Frameworks/libonnxruntime.1.21.0.dylib`, then each bundle,
with hardened runtime and a secure timestamp. The Standalone gets
`packaging/macos/Standalone.entitlements` (audio input); plug-ins get no exceptions. Bundles are
marked non-relocatable so the installer never "upgrades" a stray copy elsewhere on disk. Then
`productbuild --sign`, `notarytool submit --wait`, `stapler staple` and `spctl --assess`.

Local use: `cmake --preset mac-universal-release && cmake --build --preset mac-universal-release`,
then `packaging/macos/build_pkg.sh` (with or without the variables above exported).

**Windows** (`packaging/windows/build_installer.ps1` + `SecondOpinion.iss`, Inno Setup 6.3+):

| Component | Installs to |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\Second Opinion.vst3\` (bundle folder: DLL, `onnxruntime.dll`, `Resources\`) |
| Standalone | `C:\Program Files\Second Opinion\` (`Second Opinion.exe`, `onnxruntime.dll`, `Resources\`) plus a Start menu entry |

The plugin DLL, the exe and the setup are Authenticode signed (SHA-256, timestamped) when the
variables are set. `onnxruntime.dll` keeps Microsoft's signature. The WebView2 runtime ships with
Windows 10 (current builds) and 11; the plugin shows a message if it is missing. The `AppId` in
the `.iss` must never change.

## Known gaps before the first public release

- **macOS minimum version (Q9).** ONNX Runtime 1.21.0's macOS binary is built for macOS 13.3
  (both slices), but the plugin targets 12.0 and links it directly. Either raise
  `CMAKE_OSX_DEPLOYMENT_TARGET` and `os-version` in `packaging/macos/distribution.xml` to 13.3, or
  pin an ORT release that still supports 12. CMake prints a warning until this is decided.
- **Binary size (NFR-09).** The prebuilt macOS ORT is 67 MB per bundle. The minimal static build
  (`cmake/ort_minimal/`, ADR 0009) is the fix; the Release workflow prints sizes to track it.
- **Plug-in identity (Q1).** Company name, bundle ID and the `Brmt`/`SOp1` codes are placeholders.
  Changing them after release breaks saved sessions and AU registration, so settle them first.
- **Third-party licence page** (P6-04): ORT and Basic Pitch licences are bundled under
  `Resources/licenses` and `Resources/basic_pitch`, but the installer does not show them yet.
- **AAX** is not built (needs the Avid SDK and PACE wrapping).
