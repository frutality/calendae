#!/usr/bin/env bash
#
# Build com.github.frutality.Calendae.flatpak: a single-file Flatpak bundle
# for direct download + `flatpak install` (not a Flathub submission -- see
# the comment in the manifest about what that would additionally require).
#
#   packaging/flatpak/build-flatpak.sh <version> [out-dir]
#
# Needs flatpak + flatpak-builder, and the flathub remote added with the
# org.kde.Platform//6.9 + org.kde.Sdk//6.9 runtimes installed (--user is
# fine, no root needed):
#
#   flatpak remote-add --if-not-exists --user flathub \
#       https://flathub.org/repo/flathub.flatpakrepo
#   flatpak install --user -y flathub org.kde.Platform//6.9 org.kde.Sdk//6.9
#
set -euo pipefail

VERSION="${1:?usage: build-flatpak.sh <version> [out-dir]}"
OUT_DIR="${2:-$PWD/dist}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_ID="com.github.frutality.Calendae"
MANIFEST="$ROOT/flatpak/$APP_ID.yml"

command -v flatpak >/dev/null || { echo "flatpak not found" >&2; exit 1; }
command -v flatpak-builder >/dev/null || { echo "flatpak-builder not found" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
BUILD_DIR="$WORK/build"
REPO_DIR="$WORK/repo"

echo ">> flatpak-builder"
flatpak-builder --user --force-clean --repo="$REPO_DIR" "$BUILD_DIR" "$MANIFEST"

install -d "$OUT_DIR"
OUT="$OUT_DIR/calendae-${VERSION}.flatpak"
echo ">> flatpak build-bundle"
flatpak build-bundle "$REPO_DIR" "$OUT" "$APP_ID" master

echo ">> $OUT"
flatpak info --user "$OUT" 2>/dev/null || true
