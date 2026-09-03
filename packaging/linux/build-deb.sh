#!/usr/bin/env bash
#
# Build calendae_<version>_<arch>.deb from the AppDir that build-appimage.sh
# populated (Qt already bundled + rpath-patched by linuxdeploy).
#
#   packaging/linux/build-deb.sh <appdir> <version> [out-dir]
#
# Self-contained by design: the whole bundle installs under
# /usr/lib/calendae and /usr/bin/calendae is a symlink into it. The package
# therefore does NOT depend on the distribution's Qt (which on Ubuntu 24.04
# LTS is 6.4.2, below calendae's 6.5 floor) -- only on a small set of base
# X/font libraries present on every desktop. glibc is the real floor, so
# build this on the oldest supported image.
#
# Env:
#   DEB_MAINTAINER   "Name <email>"   (default: the project maintainer)
#
set -euo pipefail

APPDIR="${1:?usage: build-deb.sh <appdir> <version> [out-dir]}"
VERSION="${2:?version}"
OUT_DIR="${3:-$PWD/dist}"

[ -x "$APPDIR/usr/bin/calendae" ] || { echo "no usr/bin/calendae in '$APPDIR'" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb not found" >&2; exit 1; }

DEB_ARCH="$(dpkg --print-architecture)"
# SemVer pre-release '-' is not a Debian upstream-version character; '~'
# is, and sorts before the release, which is what a pre-release wants.
DEB_VERSION="${VERSION//-/'~'}"
MAINT="${DEB_MAINTAINER:-Alexander Yaskevich <5549411+frutality@users.noreply.github.com>}"
HOMEPAGE="https://github.com/frutality/calendae"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
ROOT="$WORK/pkg"

# ---------------------------------------------------------------- payload ---
install -d "$ROOT/usr/lib" "$ROOT/usr/bin" "$ROOT/usr/share/doc/calendae"
cp -a "$APPDIR/usr" "$ROOT/usr/lib/calendae"

# freedesktop metadata belongs at the real /usr/share, not inside the
# private bundle dir, or the desktop environment will not see it.
for d in applications metainfo icons; do
    if [ -d "$ROOT/usr/lib/calendae/share/$d" ]; then
        mkdir -p "$ROOT/usr/share"
        mv "$ROOT/usr/lib/calendae/share/$d" "$ROOT/usr/share/$d"
    fi
done
[ -f "$ROOT/usr/lib/calendae/share/doc/calendae/LICENSE" ] \
    && mv "$ROOT/usr/lib/calendae/share/doc/calendae/LICENSE" "$ROOT/usr/share/doc/calendae/LICENSE"
rm -rf "$ROOT/usr/lib/calendae/share"

# /usr/bin/calendae -> the bundled binary. $ORIGIN in its rpath resolves
# through the symlink to the real file's directory, so the private libs and
# plugins are still found.
ln -s ../lib/calendae/bin/calendae "$ROOT/usr/bin/calendae"

# linuxdeploy writes bin/qt.conf; provide a sane default if it did not.
if [ ! -f "$ROOT/usr/lib/calendae/bin/qt.conf" ]; then
    printf '[Paths]\nPrefix = ..\nPlugins = plugins\n' > "$ROOT/usr/lib/calendae/bin/qt.conf"
fi

# belt-and-suspenders strip
find "$ROOT/usr/lib/calendae" -type f -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true
strip --strip-unneeded "$ROOT/usr/lib/calendae/bin/calendae" 2>/dev/null || true

# ------------------------------------------------------------ doc / legal ---
: > "$ROOT/usr/share/doc/calendae/copyright"
{
    cat <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: calendae
Source: $HOMEPAGE

Files: *
Copyright: 2026 Alexander Yaskevich
License: MIT

License: MIT
EOF
    if [ -f "$ROOT/usr/share/doc/calendae/LICENSE" ]; then
        sed 's/^$/./; s/^/ /' "$ROOT/usr/share/doc/calendae/LICENSE"
    fi
} > "$ROOT/usr/share/doc/calendae/copyright"

{
    echo "calendae ($DEB_VERSION) unstable; urgency=medium"
    echo
    echo "  * Automated build from git $VERSION."
    echo
    echo " -- $MAINT  $(date -R)"
} | gzip -9n > "$ROOT/usr/share/doc/calendae/changelog.Debian.gz"

# --------------------------------------------------------------- control ----
install -d "$ROOT/DEBIAN"

EXTRA_DEPS="libssl3, desktop-file-utils, hicolor-icon-theme"
AUTO_DEPS=""
if command -v dpkg-shlibdeps >/dev/null; then
    mkdir -p "$WORK/sd/debian"
    : > "$WORK/sd/debian/control"
    mapfile -t _sos < <(find "$ROOT/usr/lib/calendae" -type f -name '*.so*')
    AUTO_DEPS="$(
        cd "$WORK/sd" &&
        dpkg-shlibdeps -O --ignore-missing-info \
            -l"$ROOT/usr/lib/calendae/lib" \
            "$ROOT/usr/lib/calendae/bin/calendae" "${_sos[@]}" 2>/dev/null |
        sed -n 's/^shlibs:Depends=//p'
    )" || AUTO_DEPS=""
fi

if [ -n "$AUTO_DEPS" ]; then
    DEPS="$AUTO_DEPS, $EXTRA_DEPS"
else
    DEPS="libc6 (>= 2.35), libstdc++6, libgcc-s1, libfontconfig1, libfreetype6, \
libdbus-1-3, libglib2.0-0, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, \
libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, \
libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-util1, \
libxcb-xfixes0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0, libpng16-16, \
libpcre2-16-0, $EXTRA_DEPS"
fi

INSTALLED_KB="$(du -sk "$ROOT/usr" | cut -f1)"

cat > "$ROOT/DEBIAN/control" <<EOF
Package: calendae
Version: $DEB_VERSION
Architecture: $DEB_ARCH
Maintainer: $MAINT
Installed-Size: $INSTALLED_KB
Depends: $DEPS
Section: utils
Priority: optional
Homepage: $HOMEPAGE
Description: lightweight Google Calendar desktop client
 Calendae is a small Qt desktop client for a single Google Calendar account:
 sign in, browse a month grid, and create, edit or delete events. It is built
 to stay light on memory.
 .
 This package bundles its own Qt runtime under /usr/lib/calendae, so it does
 not depend on the distribution's Qt version.
EOF

# dpkg triggers refresh the desktop and icon caches -- no maintainer scripts.
cat > "$ROOT/DEBIAN/triggers" <<'EOF'
activate-noawait update-desktop-database
activate-noawait /usr/share/icons/hicolor
EOF

( cd "$ROOT" && find usr -type f -print0 | xargs -0 md5sum > DEBIAN/md5sums )

# ------------------------------------------------------------------ build ---
install -d "$OUT_DIR"
OUT="$OUT_DIR/calendae_${DEB_VERSION}_${DEB_ARCH}.deb"
dpkg-deb --root-owner-group --build "$ROOT" "$OUT"
echo ">> $OUT"
dpkg-deb --info "$OUT" | sed 's/^/   /'
