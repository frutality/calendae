#!/usr/bin/env bash
#
# Tell whether the calendae that is *running* is the build that is on disk
# right now, i.e. whether you restarted it after your last rebuild. Useful
# when a change has no visible UI effect and `calendae --version` can't help
# (it is fixed at CMake configure time, so every rebuild of the same commit
# prints the same string). Linux only.
#
# Usage:
#   scripts/is-running-latest.sh [path-to-binary] [-c STRING]
#
#   path-to-binary    only look at processes started from this file, e.g.
#                     build-lean/calendae (default: every running `calendae`)
#   -c, --contains S  also require the string S to be present in the code the
#                     process is actually running. Pick a marker from the
#                     change you are verifying (a new tr() string, say); it
#                     proves this build contains the change, not merely that
#                     it is the latest build.
#
# How it works: /proc/<pid>/exe is the exact file the process was started
# from. A rebuild replaces that file (the old one becomes "(deleted)"), so a
# process that is still running the previous build shows up as STALE.
#
# Exit status: 0 every matching instance is current, 1 at least one is stale
# (or lacks the --contains marker), 2 nothing matching is running / bad usage.
#
set -euo pipefail

BIN=""
MARKER=""

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        -c|--contains)
            [ $# -ge 2 ] || { echo "$1 needs an argument (try --help)" >&2; exit 2; }
            MARKER="$2"; shift ;;
        -*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
        *)
            [ -z "$BIN" ] || { echo "only one binary path is accepted (try --help)" >&2; exit 2; }
            BIN="$1" ;;
    esac
    shift
done

if [ -n "$BIN" ]; then
    [ -e "$BIN" ] || { echo "no such file: $BIN" >&2; exit 2; }
    BIN=$(realpath "$BIN")
fi

build_id() { readelf -n "$1" 2>/dev/null | awk '/Build ID/ { print substr($3, 1, 8); exit }'; }

matched=0
stale=0

for pid in $(pgrep -x calendae || true); do
    exe=$(readlink "/proc/$pid/exe" 2>/dev/null) || continue   # not ours / already gone
    path=${exe% (deleted)}
    if [ -n "$BIN" ] && [ "$path" != "$BIN" ]; then
        continue
    fi
    matched=$((matched + 1))

    started=$(ps -o lstart= -p "$pid" | sed 's/^ *//')
    verdict=CURRENT
    why="running image is byte-identical to $path"

    if [ "$exe" != "$path" ]; then
        verdict=STALE
        why="the file it was started from has been replaced or removed since (rebuilt after launch)"
    elif ! cmp -s "/proc/$pid/exe" "$path"; then
        verdict=STALE
        why="running image differs from $path"
    fi

    if [ -n "$MARKER" ] && ! grep -aqF -- "$MARKER" "/proc/$pid/exe"; then
        verdict=STALE
        why="marker not found in the running image: $MARKER"
    fi

    [ "$verdict" = CURRENT ] || stale=1

    echo "$verdict  pid $pid  $path"
    echo "         started:  $started"
    if [ -e "$path" ]; then
        echo "         on disk:  $(stat -c '%y' "$path" | cut -d. -f1)  (build-id $(build_id "$path"))"
    fi
    echo "         running:  build-id $(build_id "/proc/$pid/exe")"
    echo "         $why"
done

if [ "$matched" -eq 0 ]; then
    if [ -n "$BIN" ]; then
        echo "not running: nothing is running from $BIN" >&2
    else
        echo "not running: no calendae process found" >&2
    fi
    exit 2
fi

exit "$stale"
