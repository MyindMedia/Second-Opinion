#!/usr/bin/env bash
# Builds "Second Opinion <version>.pkg": VST3, AU and Standalone, each an optional installer choice.
#
#   packaging/macos/build_pkg.sh [artefacts-dir] [out-dir]
#
# artefacts-dir defaults to build/mac-universal-release/SecondOpinion_artefacts/Release
# out-dir       defaults to build/packages
#
# Signing and notarization are driven by environment variables and skipped when they are unset,
# so the script always produces a (possibly unsigned) package:
#
#   APPLE_DEVELOPER_ID_APPLICATION  "Developer ID Application: <Name> (<TEAMID>)"  signs the bundles
#   APPLE_DEVELOPER_ID_INSTALLER    "Developer ID Installer: <Name> (<TEAMID>)"    signs the .pkg
#   APPLE_TEAM_ID                   <TEAMID>, checked against the signed bundles
#   APPLE_NOTARY_KEY                App Store Connect API key: path to the .p8 file, or its contents
#   APPLE_NOTARY_KEY_ID             key ID of that API key
#   APPLE_NOTARY_ISSUER             issuer UUID of that API key
#   SO_VERSION                      overrides the version read from CMakeLists.txt
#
# The identities must already be in a keychain on the search list (release.yml imports them).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
art="${1:-$repo/build/mac-universal-release/SecondOpinion_artefacts/Release}"
out="${2:-$repo/build/packages}"

version="${SO_VERSION:-$(sed -nE 's/^project\(SecondOpinion VERSION ([0-9.]+).*/\1/p' "$repo/CMakeLists.txt")}"
[ -n "$version" ] || { echo "error: could not read the version from CMakeLists.txt" >&2; exit 1; }

name="Second Opinion"
id_base="com.bermentaudio.secondopinion"
vst3="$art/VST3/$name.vst3"
au="$art/AU/$name.component"
app="$art/Standalone/$name.app"
for b in "$vst3" "$au" "$app"; do
    [ -d "$b" ] || { echo "error: missing $b (build the mac-universal-release preset first)" >&2; exit 1; }
done

work="$(mktemp -d "${TMPDIR:-/tmp}/so-pkg.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"

# ---- Stage copies (never sign the build tree in place) -------------------------------------
stage() {   # stage <bundle> <install dir> <root name>
    local root="$work/roots/$3"
    mkdir -p "$root$2"
    ditto "$1" "$root$2/$(basename "$1")"
    echo "$root$2/$(basename "$1")"
}
s_vst3="$(stage "$vst3" "/Library/Audio/Plug-Ins/VST3" vst3)"
s_au="$(stage "$au" "/Library/Audio/Plug-Ins/Components" au)"
s_app="$(stage "$app" "/Applications" app)"

# ---- Code signing (inside out: nested dylibs first, then the bundle) ------------------------
sign_bundle() {   # sign_bundle <bundle> [entitlements]
    local b="$1" ent="${2:-}"
    if [ -d "$b/Contents/Frameworks" ]; then
        find "$b/Contents/Frameworks" -type f \( -name "*.dylib" -o -perm -u+x \) -print0 |
            while IFS= read -r -d '' f; do
                codesign --force --timestamp --options runtime --sign "$APPLE_DEVELOPER_ID_APPLICATION" "$f"
            done
    fi
    local args=(--force --timestamp --options runtime --sign "$APPLE_DEVELOPER_ID_APPLICATION")
    [ -n "$ent" ] && args+=(--entitlements "$ent")
    codesign "${args[@]}" "$b"
    codesign --verify --strict --deep --verbose=2 "$b"
    if [ -n "${APPLE_TEAM_ID:-}" ]; then
        local team
        team="$(codesign -dv "$b" 2>&1 | sed -nE 's/^TeamIdentifier=(.*)/\1/p')"
        [ "$team" = "$APPLE_TEAM_ID" ] || { echo "error: $b signed by team '$team', expected '$APPLE_TEAM_ID'" >&2; exit 1; }
    fi
}

signed=0
if [ -n "${APPLE_DEVELOPER_ID_APPLICATION:-}" ]; then
    echo "Signing bundles with: $APPLE_DEVELOPER_ID_APPLICATION"
    sign_bundle "$s_vst3" "$here/Plugin.entitlements"
    sign_bundle "$s_au" "$here/Plugin.entitlements"
    sign_bundle "$s_app" "$here/Standalone.entitlements"
    signed=1
else
    echo "APPLE_DEVELOPER_ID_APPLICATION not set: bundles stay ad-hoc signed (local testing only)."
fi

# ---- Component packages --------------------------------------------------------------------
component() {   # component <root name> <identifier suffix>
    local root="$work/roots/$1" plist="$work/$1.plist"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    # Install exactly where we say, even if the user has a copy of the bundle elsewhere.
    local n
    n="$(/usr/libexec/PlistBuddy -c 'Print' "$plist" | grep -c 'RootRelativeBundlePath' || true)"
    for ((i = 0; i < n; i++)); do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
    done
    pkgbuild --root "$root" --component-plist "$plist" --identifier "$id_base.$2" \
             --version "$version" --install-location / "$work/$1.pkg"
}
component vst3 vst3
component au au
component app standalone

# ---- Product archive -----------------------------------------------------------------------
sed -e "s/@VERSION@/$version/g" "$here/distribution.xml" > "$work/distribution.xml"
pkg="$out/$name $version.pkg"
rm -f "$pkg"
product_args=(--distribution "$work/distribution.xml" --package-path "$work" --version "$version")
if [ -n "${APPLE_DEVELOPER_ID_INSTALLER:-}" ]; then
    product_args+=(--sign "$APPLE_DEVELOPER_ID_INSTALLER" --timestamp)
else
    echo "APPLE_DEVELOPER_ID_INSTALLER not set: the .pkg is unsigned."
fi
productbuild "${product_args[@]}" "$pkg"

# ---- Notarization --------------------------------------------------------------------------
if [ "$signed" = 1 ] && [ -n "${APPLE_DEVELOPER_ID_INSTALLER:-}" ] && [ -n "${APPLE_NOTARY_KEY:-}" ] \
   && [ -n "${APPLE_NOTARY_KEY_ID:-}" ] && [ -n "${APPLE_NOTARY_ISSUER:-}" ]; then
    key="$APPLE_NOTARY_KEY"
    if [ ! -f "$key" ]; then
        key="$work/notary_key.p8"
        ( umask 077; printf '%s\n' "$APPLE_NOTARY_KEY" > "$key" )
    fi
    echo "Submitting to the notary service (this can take a few minutes)..."
    xcrun notarytool submit "$pkg" --key "$key" --key-id "$APPLE_NOTARY_KEY_ID" \
        --issuer "$APPLE_NOTARY_ISSUER" --wait --timeout 1h
    xcrun stapler staple "$pkg"
    xcrun stapler validate "$pkg"
    spctl --assess --type install --verbose=2 "$pkg"
else
    echo "Notarization skipped (needs both Developer ID identities and APPLE_NOTARY_KEY, _KEY_ID, _ISSUER)."
fi

echo "Package: $pkg"
