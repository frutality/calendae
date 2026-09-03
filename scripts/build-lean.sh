#!/usr/bin/env bash
#
# Low-RAM build: calendae against the stripped static Qt produced by
# scripts/build-qt-lean.sh.
#
#   Result: ~50 MB RSS / ~32 MB PSS  (vs ~68 / ~45 for build-standard.sh)
#
# Trade-offs baked in here:
#   * no compiled translations (the one English catalogue is dropped;
#     source strings are already English)
#   * unit tests are not built (the lean Qt has no QtTest) -- run the test
#     suite from build-standard.sh instead
#   * statically linked: a Qt rebuild means rebuilding calendae too
#
# Usage:
#   scripts/build-lean.sh [--clean] [--run]
#
#     --clean   wipe the build dir first
#     --run     launch the binary when the build finishes
#
# Environment overrides:
#   QT_VERSION      default 6.11.1
#   QT_LEAN_PREFIX  lean Qt install.  Default: ~/Qt/$QT_VERSION-lean-static
#   BUILD_DIR       Default: <repo>/build-lean
#   JOBS            Default: nproc
#
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

QT_VERSION="${QT_VERSION:-6.11.1}"
QT_LEAN_PREFIX="${QT_LEAN_PREFIX:-$HOME/Qt/${QT_VERSION}-lean-static}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-lean}"
JOBS="${JOBS:-$(nproc)}"
RUN=0

for arg in "$@"; do
    case "$arg" in
        --clean) echo ">> rm -rf $BUILD_DIR"; rm -rf "$BUILD_DIR" ;;
        --run)   RUN=1 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

if [ ! -e "$QT_LEAN_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "lean Qt not found at: $QT_LEAN_PREFIX" >&2
    echo "run scripts/build-qt-lean.sh first." >&2
    exit 1
fi

# Reuse the qtkeychain checkout that build-standard.sh already fetched, so
# this configure step works with no network. Harmless if absent -- CMake's
# FetchContent will clone it as usual.
keychain_src="$ROOT/build/_deps/qtkeychain-src"
keychain_arg=()
[ -d "$keychain_src" ] && keychain_arg=( -DFETCHCONTENT_SOURCE_DIR_QTKEYCHAIN="$keychain_src" )

echo ">> configure  (lean static Qt at $QT_LEAN_PREFIX)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_PREFIX_PATH="$QT_LEAN_PREFIX" \
    -DCALENDAE_BUILD_TRANSLATIONS=OFF \
    -DTINY_GCAL_BUILD_TESTS=OFF \
    -DCMAKE_CXX_FLAGS="-ffunction-sections -fdata-sections" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -Wl,--as-needed" \
    "${keychain_arg[@]}"

echo ">> build  ($JOBS jobs)"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

command -v strip >/dev/null && strip "$BUILD_DIR/calendae"

echo ">> done: $BUILD_DIR/calendae"
echo "   measure:  scripts/measure-memory.sh $BUILD_DIR/calendae"

[ "$RUN" = 1 ] && exec "$BUILD_DIR/calendae"
