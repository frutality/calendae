#!/usr/bin/env bash
#
# Standard build: calendae against a full, dynamically-linked Qt (the Qt
# online-installer SDK, or a distro's -dev packages). This is the build to
# use for development and for running the test suite.
#
#   Result: ~68 MB RSS / ~45 MB PSS at runtime.
#   For the low-RAM build see scripts/build-lean.sh.
#
# Usage:
#   scripts/build-standard.sh [--debug|--release] [--no-tests] [--clean]
#
# Environment overrides:
#   QT_VERSION   Qt version, only used to build the default QT_PREFIX (6.11.1)
#   QT_PREFIX    Qt install dir (has bin/qmake, lib/cmake/Qt6).
#                Default: ~/Qt/$QT_VERSION/gcc_64
#   BUILD_DIR    Default: <repo>/build
#   JOBS         Parallel compile jobs. Default: nproc
#
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

QT_VERSION="${QT_VERSION:-6.11.1}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/${QT_VERSION}/gcc_64}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
JOBS="${JOBS:-$(nproc)}"
BUILD_TYPE=Release
RUN_TESTS=1

for arg in "$@"; do
    case "$arg" in
        --debug)    BUILD_TYPE=Debug ;;
        --release)  BUILD_TYPE=Release ;;
        --no-tests) RUN_TESTS=0 ;;
        --clean)    echo ">> rm -rf $BUILD_DIR"; rm -rf "$BUILD_DIR" ;;
        -h|--help)  sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

if [ ! -e "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Qt not found under: $QT_PREFIX" >&2
    echo "Point QT_PREFIX at your Qt install, or install the system packages" >&2
    echo "and set QT_PREFIX=/usr (see docs/BUILDING.md)." >&2
    exit 1
fi

echo ">> configure  ($BUILD_TYPE, Qt at $QT_PREFIX)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX"

echo ">> build  ($JOBS jobs)"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [ "$RUN_TESTS" = 1 ]; then
    echo ">> ctest"
    ( cd "$BUILD_DIR" && ctest --output-on-failure )
fi

echo ">> done: $BUILD_DIR/calendae"
