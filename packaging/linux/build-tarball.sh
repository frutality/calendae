#!/usr/bin/env bash
#
# Roll a portable tarball from an AppDir already populated by
# build-appimage.sh (linuxdeploy has copied Qt in and patched rpaths).
# Produces calendae-<version>-linux-<arch>.tar.gz whose top-level
# `calendae` launcher sets the library and plugin paths.
#
#   packaging/linux/build-tarball.sh <appdir> <version> [out-dir]
#
set -euo pipefail

APPDIR="${1:?usage: build-tarball.sh <appdir> <version> [out-dir]}"
VERSION="${2:?version}"
OUT_DIR="${3:-$PWD/dist}"
ARCH="$(uname -m)"

[ -x "$APPDIR/usr/bin/calendae" ] || { echo "no usr/bin/calendae in '$APPDIR'" >&2; exit 1; }

STAGE_NAME="calendae-$VERSION-linux-$ARCH"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/$STAGE_NAME"

install -d "$STAGE"
cp -a "$APPDIR/usr" "$STAGE/usr"
[ -f "$STAGE/usr/share/doc/calendae/LICENSE" ] && cp "$STAGE/usr/share/doc/calendae/LICENSE" "$STAGE/"

cat > "$STAGE/calendae" <<'SH'
#!/bin/sh
# Portable launcher: resolve our own directory and point Qt at the bundled
# libraries and plugins before handing off to the real binary.
here=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$here/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$here/usr/plugins"
exec "$here/usr/bin/calendae" "$@"
SH
chmod +x "$STAGE/calendae"

install -d "$OUT_DIR"
tar -C "$WORK" -czf "$OUT_DIR/$STAGE_NAME.tar.gz" "$STAGE_NAME"
echo ">> $OUT_DIR/$STAGE_NAME.tar.gz"
