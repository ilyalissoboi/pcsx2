#!/bin/bash

# Turns the downloaded CI artifacts of a fork release into the final release assets and notes.
# Fails if a package is missing librashader or emusync, since both silently compile out of a build.

# Inputs as env-vars
# TAG_VAL    - the release tag, e.g. v2.9.147-sc1
# SCAN_DIR   - directory holding one folder per downloaded artifact
# OUT_DIR    - where the final assets are written
# NOTES_FILE - where the release notes are written

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fail() {
	echo "::error::$1"
	exit 1
}

# The symbols artifact isn't part of a fork release.
rm -rf "${SCAN_DIR:?}"/*-symbols*

shopt -s nullglob
windows_dirs=("$SCAN_DIR"/*windows*/)
macos_archives=("$SCAN_DIR"/*macos*/*.tar.xz)
shopt -u nullglob
[ "${#windows_dirs[@]}" -eq 1 ] || fail "Expected one Windows artifact, found ${#windows_dirs[@]}"
[ "${#macos_archives[@]}" -eq 1 ] || fail "Expected one macOS archive, found ${#macos_archives[@]}"
windows_dir="${windows_dirs[0]}"

for lib in librashader.dll emusync.dll; do
	[ -f "$windows_dir/$lib" ] || fail "Windows package is missing $lib"
done

# Read the listing first: grep -q closing the pipe early would fail tar under pipefail.
macos_listing="$(tar --warning=no-unknown-keyword -tJf "${macos_archives[0]}")"
for lib in librashader.dylib libemusync.dylib; do
	grep -q "/Contents/Frameworks/$lib\$" <<< "$macos_listing" || fail "macOS package is missing $lib"
done

(cd "$windows_dir" && 7z a -mx=9 asset.7z ./*)

mkdir -p "$OUT_DIR"
python "$SCRIPT_DIR/rename-release-assets.py"

windows_asset="pcsx2-$TAG_VAL-windows-x64-Qt.7z"
macos_asset="pcsx2-$TAG_VAL-macos-Qt.tar.xz"
for asset in "$windows_asset" "$macos_asset"; do
	[ -f "$OUT_DIR/$asset" ] || fail "Expected release asset $asset was not produced"
done

windows_sha="$(sha256sum "$OUT_DIR/$windows_asset" | cut -d' ' -f1)"
macos_sha="$(sha256sum "$OUT_DIR/$macos_asset" | cut -d' ' -f1)"

cat > "$NOTES_FILE" << EOF
Unofficial fork build of PCSX2 ${TAG_VAL%-sc*} with the librashader post-processing shader chain and memory card sync. Not affiliated with the PCSX2 team; do not report issues with these builds upstream.

## Packages

| File | Platform | SHA-256 |
|---|---|---|
| \`$windows_asset\` | Windows x64 (CMake clang-cl, LTO) | \`$windows_sha\` |
| \`$macos_asset\` | macOS arm64 (Apple Silicon) | \`$macos_sha\` |

## Notes

- macOS: the app is ad-hoc signed and not notarized. On first launch, right-click the app and choose Open, or run \`xattr -dr com.apple.quarantine PCSX2-$TAG_VAL.app\`.
- The macOS build is arm64 only.
- Memory card sync needs a bucket on an S3-compatible service (AWS S3, Cloudflare R2, Backblaze B2, MinIO, …) and an access key for it.
EOF

cat "$NOTES_FILE"
