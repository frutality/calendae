#!/usr/bin/env bash
#
# Validate the freedesktop .desktop entry and the AppStream metainfo file
# that every Linux packaging format (deb, AppImage, Flatpak) consumes.
#
# Both files are generated at configure time (they embed CALENDAE_APP_ID),
# so this runs against a build directory, not the source tree.
#
#   scripts/lint-metadata.sh [build-dir]        (default: build)
#
# Exits 77 (CTest "skipped") when neither validator is installed:
#   desktop-file-validate  <- desktop-file-utils
#   appstreamcli           <- appstream
#
set -euo pipefail

BUILD_DIR="${1:-build}"
cache="$BUILD_DIR/CMakeCache.txt"
[ -f "$cache" ] || { echo "no CMakeCache.txt in '$BUILD_DIR' -- configure first" >&2; exit 1; }

appid=$(sed -n 's/^CALENDAE_APP_ID:[^=]*=//p' "$cache")
[ -n "$appid" ] || { echo "CALENDAE_APP_ID not found in $cache" >&2; exit 1; }

desktop="$BUILD_DIR/$appid.desktop"
metainfo="$BUILD_DIR/$appid.metainfo.xml"

have_desktop=0; command -v desktop-file-validate >/dev/null && have_desktop=1
have_appstream=0; command -v appstreamcli >/dev/null && have_appstream=1

if [ "$have_desktop" = 0 ] && [ "$have_appstream" = 0 ]; then
    echo "SKIP: install desktop-file-utils and/or appstream to run this lint" >&2
    exit 77
fi

rc=0

if [ "$have_desktop" = 1 ]; then
    echo ">> desktop-file-validate $desktop"
    desktop-file-validate "$desktop" || rc=1
else
    echo "-- desktop-file-validate not installed (desktop-file-utils), skipping"
fi

if [ "$have_appstream" = 1 ]; then
    echo ">> appstreamcli validate $metainfo"
    appstreamcli validate --no-net --explain "$metainfo" || rc=1
else
    echo "-- appstreamcli not installed (appstream), skipping"
fi

exit $rc
