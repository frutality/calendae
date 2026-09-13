#!/usr/bin/env bash
#
# Measure test coverage: configure+build with --coverage instrumentation
# (-DCALENDAE_ENABLE_COVERAGE=ON), run ctest to exercise it, then turn the
# resulting .gcda files into an HTML report with gcovr.
#
# Only tgc_auth/tgc_calendar are meaningfully covered -- calendae's own
# code (src/app/) is just MainWindow wiring, per CLAUDE.md, and the tests
# link tgc_calendar/tgc_auth directly rather than the calendae executable.
#
# Usage:
#   scripts/measure-coverage.sh [--open] [--clean]
#
#   --open   open the HTML report in a browser (xdg-open) when done
#   --clean  wipe BUILD_DIR first (a stale non-coverage build dir will not
#            have --coverage flags; --clean is the fix if you reuse a dir
#            that was previously configured without CALENDAE_ENABLE_COVERAGE)
#
# Requires: gcovr (pip install gcovr, or your distro's gcovr package).
#
# Environment overrides:
#   QT_VERSION   Qt version, only used to build the default QT_PREFIX (6.11.1)
#   QT_PREFIX    Qt install dir (has bin/qmake, lib/cmake/Qt6).
#                Default: ~/Qt/$QT_VERSION/gcc_64
#   BUILD_DIR    Default: <repo>/build-coverage (kept separate from build/ --
#                a coverage build is instrumented, slower, and its .gcda
#                files only make sense together with one specific ctest run)
#   OUT_DIR      Where the HTML report is written. Default: <repo>/coverage
#   JOBS         Parallel compile jobs. Default: nproc
#
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

QT_VERSION="${QT_VERSION:-6.11.1}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/${QT_VERSION}/gcc_64}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-coverage}"
OUT_DIR="${OUT_DIR:-$ROOT/coverage}"
JOBS="${JOBS:-$(nproc)}"
OPEN_REPORT=0

for arg in "$@"; do
    case "$arg" in
        --open)    OPEN_REPORT=1 ;;
        --clean)   echo ">> rm -rf $BUILD_DIR"; rm -rf "$BUILD_DIR" ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

command -v gcovr >/dev/null || {
    echo "gcovr not found. Install it: pip install gcovr  (or: apt install gcovr)" >&2
    exit 1
}

if [ ! -e "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Qt not found under: $QT_PREFIX" >&2
    echo "Point QT_PREFIX at your Qt install (see docs/BUILDING.md)." >&2
    exit 1
fi

echo ">> configure  (Debug + coverage instrumentation, Qt at $QT_PREFIX)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DCALENDAE_ENABLE_COVERAGE=ON \
    -DTINY_GCAL_BUILD_TESTS=ON

echo ">> build  ($JOBS jobs)"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ">> ctest  (this is what generates the .gcda hit counts)"
( cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure )

echo ">> gcovr"
mkdir -p "$OUT_DIR"
gcovr \
    --root "$ROOT" \
    --filter 'src/(auth|calendar)/' \
    --exclude-unreachable-branches \
    --exclude-throw-branches \
    --html-details "$OUT_DIR/index.html" \
    --print-summary \
    "$BUILD_DIR" | tee "$OUT_DIR/summary.txt"

echo
echo ">> report: $OUT_DIR/index.html"

# In CI (GITHUB_STEP_SUMMARY set), also surface the summary in the run's Job
# Summary tab so nobody has to download the report artifact just to see the
# headline percentage.
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### Test coverage (src/auth + src/calendar)"
        echo '```'
        cat "$OUT_DIR/summary.txt"
        echo '```'
    } >> "$GITHUB_STEP_SUMMARY"
fi

if [ "$OPEN_REPORT" = 1 ]; then
    xdg-open "$OUT_DIR/index.html" >/dev/null 2>&1 || echo "(xdg-open failed -- open the file manually)"
fi
