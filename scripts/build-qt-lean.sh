#!/usr/bin/env bash
#
# Build a stripped-down, statically-linked Qt (qtbase only) that calendae
# then links against for a low-RAM binary. Run this once; re-run to rebuild
# or to move to a new Qt version. Takes ~20-40 min and ~4 GB of disk in the
# build tree (the installed result is a few hundred MB).
#
# This does NOT touch the system or your Qt SDK install; everything lands
# under QT_LEAN_PREFIX (default ~/Qt/<version>-lean-static).
#
# ---------------------------------------------------------------------------
# One-time prerequisites (Ubuntu / Debian) -- headers to build Qt's xcb
# platform plugin and font stack. Install them yourself:
#
#   sudo apt install --no-install-recommends \
#     libgl1-mesa-dev libdbus-1-dev libssl-dev zlib1g-dev \
#     libfontconfig1-dev libfreetype-dev \
#     libx11-xcb-dev libxkbcommon-dev libxkbcommon-x11-dev \
#     libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-image0-dev \
#     libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render0-dev \
#     libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
#     libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
#     libxcb-xinerama0-dev libxcb-xkb-dev
#
# Also needs: git, cmake >= 3.21, ninja, a C++17 compiler, perl, python3.
# ---------------------------------------------------------------------------
#
# Usage:
#   scripts/build-qt-lean.sh [--no-ltcg | --ltcg] [--reconfigure]
#
#     --no-ltcg      skip link-time optimisation (much faster build, the
#                    resulting binary is ~1 MB larger in RSS). Default for
#                    QT_LINKAGE=shared.
#     --ltcg         force link-time optimisation on. Default for static.
#     --reconfigure  wipe the Qt build dir and configure from scratch
#
# Environment overrides:
#   QT_VERSION       default 6.11.1  (git tag v$QT_VERSION of qtbase)
#   QT_LINKAGE       'static' (default) or 'shared'. static = smallest RSS,
#                    for personal use. shared = what the release pipeline
#                    ships: Qt stays replaceable .so files, so LGPL relinking
#                    is trivial, at a small memory cost. Each linkage has its
#                    own build dir and install prefix.
#   QT_SRC_DIR       qtbase checkout.        Default: ~/src/qtbase
#   QT_BUILD_DIR     out-of-source build.    Default: $QT_SRC_DIR-build-lean[-shared]
#   QT_LEAN_PREFIX   install prefix.         Default: ~/Qt/$QT_VERSION-lean-{static,shared}
#   JOBS             parallel jobs.          Default: nproc
#
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.11.1}"
QT_TAG="v${QT_VERSION}"
QT_SRC_DIR="${QT_SRC_DIR:-$HOME/src/qtbase}"

QT_LINKAGE="${QT_LINKAGE:-static}"
case "$QT_LINKAGE" in
    static) _prefix_tag="lean-static"; _build_tag="lean";        _link_flag="-static"; LTCG=1 ;;
    shared) _prefix_tag="lean-shared"; _build_tag="lean-shared"; _link_flag="-shared"; LTCG=0 ;;
    *) echo "QT_LINKAGE must be 'static' or 'shared' (got '$QT_LINKAGE')" >&2; exit 2 ;;
esac

QT_BUILD_DIR="${QT_BUILD_DIR:-${QT_SRC_DIR}-build-${_build_tag}}"
QT_LEAN_PREFIX="${QT_LEAN_PREFIX:-$HOME/Qt/${QT_VERSION}-${_prefix_tag}}"
JOBS="${JOBS:-$(nproc)}"

RECONFIGURE=0
for arg in "$@"; do
    case "$arg" in
        --no-ltcg)     LTCG=0 ;;
        --ltcg)        LTCG=1 ;;
        --reconfigure) RECONFIGURE=1 ;;
        -h|--help)     sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

# --- 1. qtbase source --------------------------------------------------------
if [ ! -d "$QT_SRC_DIR/.git" ]; then
    echo ">> cloning qtbase $QT_TAG  ->  $QT_SRC_DIR"
    git clone --branch "$QT_TAG" --depth 1 \
        https://github.com/qt/qtbase.git "$QT_SRC_DIR"
else
    have=$(git -C "$QT_SRC_DIR" describe --tags --always 2>/dev/null || echo '?')
    echo ">> qtbase source present ($have) at $QT_SRC_DIR"
    if [ "$have" != "$QT_TAG" ]; then
        echo "   WARNING: checkout is $have, not $QT_TAG."
        echo "   To switch: rm -rf '$QT_SRC_DIR' '$QT_BUILD_DIR' and re-run."
    fi
fi

# --- 2. configure ----------------------------------------------------------
# Why each flag: see docs/BUILDING.md. Short version:
#   -static             link Qt into calendae so --gc-sections can drop the
#                       ~2/3 of Qt code the app never calls
#   -no-icu             biggest single win: no bundled ICU (~2.5 MB resident
#                       + a 30 MB data file mapping). Qt's own CLDR still
#                       does locale date/number formatting; only ICU-grade
#                       locale-aware *string collation* is lost, which
#                       calendae does not use.
#   -optimize-size      -Os: smaller code pages
#   -ltcg               link-time optimisation, another ~1 MB off
#   -system-freetype    keep freetype/fontconfig/zlib as shared .so so they
#   -fontconfig         stay shared with the rest of the desktop instead of
#   -system-zlib        being baked privately into the static blob
#   -no-*               drop OpenGL / Vulkan / SQL / QtConcurrent / print /
#                       QtTest -- none are used
CONFIGURE_FLAGS=(
    -prefix "$QT_LEAN_PREFIX"
    "$_link_flag" -release -optimize-size
    -no-icu
    -no-opengl -no-feature-vulkan
    -no-feature-printsupport -no-feature-concurrent
    -no-feature-sql -no-feature-testlib
    -system-freetype -fontconfig -system-zlib
    -dbus-runtime -openssl-runtime
    -nomake examples -nomake tests
)
[ "$LTCG" = 1 ] && CONFIGURE_FLAGS+=( -ltcg )

[ "$RECONFIGURE" = 1 ] && { echo ">> rm -rf $QT_BUILD_DIR"; rm -rf "$QT_BUILD_DIR"; }
mkdir -p "$QT_BUILD_DIR"

if [ ! -f "$QT_BUILD_DIR/CMakeCache.txt" ]; then
    echo ">> configure qtbase (ltcg=$LTCG)  ->  $QT_LEAN_PREFIX"
    ( cd "$QT_BUILD_DIR" && "$QT_SRC_DIR/configure" "${CONFIGURE_FLAGS[@]}" )
else
    echo ">> reusing configuration in $QT_BUILD_DIR  (--reconfigure to redo)"
fi

# --- 3. build + install --------------------------------------------------------
echo ">> build qtbase  ($JOBS jobs -- the slow part)"
cmake --build "$QT_BUILD_DIR" --parallel "$JOBS"

echo ">> install  ->  $QT_LEAN_PREFIX"
cmake --install "$QT_BUILD_DIR"

echo ">> lean Qt ready at $QT_LEAN_PREFIX"
echo "   next: scripts/build-lean.sh"
