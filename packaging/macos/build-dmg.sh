#!/usr/bin/env bash
#
# Build calendae.app as a universal (arm64 + x86_64) bundle, let macdeployqt
# fold the Qt frameworks in, ad-hoc codesign it, and wrap it in a .dmg.
#
#   packaging/macos/build-dmg.sh <version> <qt-dir> [out-dir]
#
#   <qt-dir>   the Qt kit dir, e.g. ~/Qt/6.8.3/macos  (must have bin/macdeployqt)
#
# The Qt macOS binary package is already universal, so one runner produces a
# bundle that runs natively on both Apple Silicon and Intel.
#
# NOT notarized and NOT signed with a Developer ID -- just ad-hoc (`-`),
# which is the minimum Apple Silicon needs to run it at all. A copy
# downloaded through a browser is still quarantined; see docs/BUILDING.md.
#
set -euo pipefail

VERSION="${1:?usage: build-dmg.sh <version> <qt-dir> [out-dir]}"
QT_DIR="${2:?qt kit dir, e.g. ~/Qt/6.8.3/macos}"
OUT_DIR="${3:-$PWD/dist}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/build-macos"
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"

[ -x "$MACDEPLOYQT" ] || { echo "macdeployqt not found at '$MACDEPLOYQT'" >&2; exit 1; }

echo ">> configure"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCALENDAE_VERSION="$VERSION" \
    -DTINY_GCAL_BUILD_TESTS=OFF \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0

echo ">> build"
cmake --build "$BUILD_DIR" --parallel

APP="$BUILD_DIR/calendae.app"
[ -d "$APP" ] || { echo "no bundle at '$APP'" >&2; exit 1; }

echo ">> macdeployqt"
"$MACDEPLOYQT" "$APP"

# Apple Silicon kills unsigned binaries on launch; ad-hoc signing is the
# floor. --deep so the folded-in frameworks/plugins are covered too.
echo ">> ad-hoc codesign"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo ">> dmg"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

mkdir -p "$OUT_DIR"
DMG="$OUT_DIR/calendae-$VERSION-macos.dmg"
rm -f "$DMG"
hdiutil create -volname "Calendae" -srcfolder "$STAGE" -ov -format UDZO "$DMG"

echo ">> $DMG"
