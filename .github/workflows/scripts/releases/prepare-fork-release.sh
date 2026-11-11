#!/bin/bash

# Turns the downloaded CI artifacts of a fork release into the final release assets and writes the notes.
# Fails if a package is missing librashader or emusync, since both silently compile out of a build.
#
# A release is published in two steps, so a shortage of macOS runners doesn't hold back the other
# platforms: Windows and Flatpak first, then macOS when its build finishes. The notes list every asset
# in OUT_DIR, so the macOS step downloads the published assets there first. With PLATFORMS empty, the
# script only rewrites the notes, e.g. after uploading a macOS package that was built elsewhere.

# Inputs as env-vars
# TAG_VAL    - the release tag, e.g. v2.9.147-sc1
# PLATFORMS  - the artifacts in SCAN_DIR to package, any of: windows macos flatpak
# SCAN_DIR   - directory holding one folder per downloaded artifact
# OUT_DIR    - where the final assets are written; assets already there are included in the notes
# NOTES_FILE - where the release notes are written

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLATFORMS="${PLATFORMS:-}"

fail() {
	echo "::error::$1"
	exit 1
}

windows_asset="pcsx2-$TAG_VAL-windows-x64-Qt.7z"
macos_asset="pcsx2-$TAG_VAL-macos-Qt.tar.xz"
flatpak_asset="pcsx2-$TAG_VAL-linux-flatpak-x64-Qt.flatpak"

mkdir -p "$OUT_DIR"

if [ -n "$PLATFORMS" ]; then
	# The symbols artifact isn't part of a fork release.
	rm -rf "${SCAN_DIR:?}"/*-symbols*

	shopt -s nullglob
	for platform in $PLATFORMS; do
		case "$platform" in
		windows)
			windows_dirs=("$SCAN_DIR"/*windows*/)
			[ "${#windows_dirs[@]}" -eq 1 ] || fail "Expected one Windows artifact, found ${#windows_dirs[@]}"
			for lib in librashader.dll emusync.dll; do
				[ -f "${windows_dirs[0]}$lib" ] || fail "Windows package is missing $lib"
			done
			(cd "${windows_dirs[0]}" && 7z a -mx=9 asset.7z ./*)
			;;
		macos)
			# Its libraries are checked on the final asset below.
			macos_archives=("$SCAN_DIR"/*macos*/*.tar.xz)
			[ "${#macos_archives[@]}" -eq 1 ] || fail "Expected one macOS archive, found ${#macos_archives[@]}"
			;;
		flatpak)
			# The Flatpak manifest checks for both libraries itself, before the bundle is made.
			flatpak_bundles=("$SCAN_DIR"/*flatpak*/*.flatpak)
			[ "${#flatpak_bundles[@]}" -eq 1 ] || fail "Expected one Flatpak bundle, found ${#flatpak_bundles[@]}"
			;;
		*)
			fail "Unknown platform $platform"
			;;
		esac
	done
	shopt -u nullglob

	python "$SCRIPT_DIR/rename-release-assets.py"

	for platform in $PLATFORMS; do
		asset_var="${platform}_asset"
		[ -f "$OUT_DIR/${!asset_var}" ] || fail "Expected release asset ${!asset_var} was not produced"
	done
fi

# The notes describe a release that already has the Windows and Flatpak packages.
for asset in "$windows_asset" "$flatpak_asset"; do
	[ -f "$OUT_DIR/$asset" ] || fail "Release asset $asset is missing from $OUT_DIR"
done

# Checked here rather than on the artifact, so a macOS package built elsewhere is checked too.
has_macos=false
if [ -f "$OUT_DIR/$macos_asset" ]; then
	# Read the listing first: grep -q closing the pipe early would fail tar under pipefail.
	macos_listing="$(tar --warning=no-unknown-keyword -tJf "$OUT_DIR/$macos_asset")"
	for lib in librashader.dylib libemusync.dylib; do
		grep -q "/Contents/Frameworks/$lib\$" <<< "$macos_listing" || fail "macOS package is missing $lib"
	done
	has_macos=true
fi

package_row() {
	local sha
	sha="$(sha256sum "$OUT_DIR/$1" | cut -d' ' -f1)"
	echo "| \`$1\` | $2 | \`$sha\` |"
}

{
	echo "Unofficial fork build of PCSX2 ${TAG_VAL%-sc*} with the librashader post-processing shader chain and memory card sync. Not affiliated with the PCSX2 team; do not report issues with these builds upstream."
	echo
	echo "## Packages"
	echo
	echo "| File | Platform | SHA-256 |"
	echo "|---|---|---|"
	package_row "$windows_asset" "Windows x64 (CMake clang-cl, LTO)"
	if $has_macos; then
		package_row "$macos_asset" "macOS arm64 (Apple Silicon)"
	fi
	package_row "$flatpak_asset" "Linux x64 (Flatpak)"
	if ! $has_macos; then
		echo
		echo "The macOS package is added to this release when its build finishes."
	fi
	echo
	echo "## Notes"
	echo
	if $has_macos; then
		echo "- macOS: the app is ad-hoc signed and not notarized. On first launch, right-click the app and choose Open, or run \`xattr -dr com.apple.quarantine PCSX2-$TAG_VAL.app\`."
		echo "- The macOS build is arm64 only."
	fi
	echo "- Linux: install with \`flatpak install --user $flatpak_asset\`. It uses its own app ID (\`io.github.ilyalissoboi.pcsx2\`), so it installs alongside the official Flatpak and keeps separate settings. The shader chain needs the Vulkan renderer."
	echo "- Memory card sync needs a bucket on an S3-compatible service (AWS S3, Cloudflare R2, Backblaze B2, MinIO, …) and an access key for it."
} > "$NOTES_FILE"

cat "$NOTES_FILE"
