#!/usr/bin/env bash
#
# Package calendae as an AppImage from a build linked against the shared
# lean Qt (scripts/build-lean.sh with QT_LINKAGE=shared).
#
#   packaging/linux/build-appimage.sh <build-dir> <lean-qt-prefix> [out-dir]
#
#     build-dir       a built build-lean-shared tree
#     lean-qt-prefix  the shared lean Qt install (has bin/qmake)
#     out-dir         where the .AppImage lands (default: ./dist)
#
# Requires linuxdeploy and linuxdeploy-plugin-qt on PATH. On CI runners
# without FUSE, export APPIMAGE_EXTRACT_AND_RUN=1 first.
#
# Leaves a fully-populated AppDir at <build-dir>/AppDir (Qt copied in,
# rpaths patched) which build-tarball.sh then reuses.
#
set -euo pipefail

BUILD_DIR="${1:?usage: build-appimage.sh <build-dir> <lean-qt-prefix> [out-dir]}"
QT_PREFIX="${2:?lean Qt prefix (has bin/qmake)}"
OUT_DIR="${3:-$PWD/dist}"

[ -x "$BUILD_DIR/calendae" ]    || { echo "no calendae in '$BUILD_DIR' -- build it first" >&2; exit 1; }
[ -x "$QT_PREFIX/bin/qmake" ]   || { echo "no qmake in '$QT_PREFIX/bin'" >&2; exit 1; }
command -v linuxdeploy           >/dev/null || { echo "linuxdeploy not on PATH" >&2; exit 1; }
command -v linuxdeploy-plugin-qt >/dev/null || { echo "linuxdeploy-plugin-qt not on PATH" >&2; exit 1; }

ARCH="$(uname -m)"
VERSION="$("$BUILD_DIR/calendae" --version | awk '{print $NF}')"
APPID="$(sed -n 's/^CALENDAE_APP_ID:[^=]*=//p' "$BUILD_DIR/CMakeCache.txt")"
[ -n "$APPID" ] || { echo "CALENDAE_APP_ID not in $BUILD_DIR/CMakeCache.txt" >&2; exit 1; }

APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null

# The vendored static QtKeychain installs dev-only files (headers, a .a, its
# CMake package, mkspecs). None of that belongs in a runtime bundle.
rm -rf "$APPDIR/usr/include" "$APPDIR/usr/mkspecs" "$APPDIR/usr/lib/cmake"
find "$APPDIR/usr/lib" -maxdepth 1 -name '*.a' -delete 2>/dev/null || true

echo ">> AppDir staged (app id $APPID, version $VERSION, arch $ARCH)"

install -d "$OUT_DIR"

export QMAKE="$QT_PREFIX/bin/qmake"
export LD_LIBRARY_PATH="$QT_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# QtNetwork dlopen()s its TLS backend, so it is not in the ELF NEEDED list
# that linuxdeploy-plugin-qt scans -- name it explicitly.
export EXTRA_QT_PLUGINS="tls"
export OUTPUT="calendae-$VERSION-$ARCH.AppImage"

linuxdeploy \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/calendae" \
    --desktop-file "$APPDIR/usr/share/applications/$APPID.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APPID.png" \
    --plugin qt \
    --output appimage

mv -f "$OUTPUT" "$OUT_DIR/$OUTPUT"
echo ">> $OUT_DIR/$OUTPUT"
