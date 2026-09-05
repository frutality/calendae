# Building calendae

There are two build flavours:

| | Standard | Lean (low-RAM) |
|---|---|---|
| Qt | full, dynamically linked (Qt SDK or distro packages) | stripped-down, **static**, built from source once |
| Runtime cost | ~68 MB RSS / ~45 MB PSS | ~50 MB RSS / ~32 MB PSS |
| Translations | compiled | dropped (source strings are English anyway) |
| Unit tests | built and run | not built (lean Qt has no QtTest) |
| Use it for | day-to-day development, running the test suite | shipping / measuring the memory target |

Each has a script under `scripts/`. All scripts take `--help`, read a few
`ENV_VAR` overrides (documented in their headers), and default `JOBS` to
`nproc`.

---

## Standard build

### Dependencies

Qt 6.4+ with Core, Gui, Widgets, Network, DBus and (unless you pass
`-DCALENDAE_BUILD_TRANSLATIONS=OFF`) LinguistTools. Plus Git and a C++17
compiler. QtKeychain is fetched automatically at configure time via
`FetchContent` (pinned to v0.14.0).

* **Qt online-installer SDK:** point `QT_PREFIX` at e.g.
  `~/Qt/6.11.1/gcc_64`.
* **Distro Qt (Ubuntu/Debian):** `sudo apt install qt6-base-dev
  qt6-tools-dev` and set `QT_PREFIX=/usr`. Note Ubuntu 24.04 ships Qt
  6.4.2; that still builds calendae (the `find_package` floor is 6.4).

### Build

```sh
scripts/build-standard.sh                 # Release, runs ctest
scripts/build-standard.sh --debug --no-tests
QT_PREFIX=/usr scripts/build-standard.sh   # against system Qt
```

Equivalent by hand:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.y/gcc_64
cmake --build build --parallel
cd build && ctest --output-on-failure
```

Run it: `build/calendae`.

---

## Lean (low-RAM) build

The app's own heap is only ~13 MB; the rest of a standard build's RSS is
Qt/OpenSSL/fontconfig library pages. The lean build attacks that by
linking a **static, feature-reduced Qt** so the linker can discard every
Qt function calendae never calls, and by dropping ICU.

### Step 1 (once): system headers for building Qt

The lean Qt build compiles Qt's `xcb` platform plugin and font stack, which
need these `-dev` packages. **Install them yourself** — no script touches
the system:

```sh
sudo apt install --no-install-recommends \
  libgl1-mesa-dev libdbus-1-dev libssl-dev zlib1g-dev \
  libfontconfig1-dev libfreetype-dev \
  libx11-xcb-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-image0-dev \
  libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render0-dev \
  libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
  libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
  libxcb-xinerama0-dev libxcb-xkb-dev
```

Also needs `git`, `cmake >= 3.21`, `ninja`, `perl`, `python3` (all already
present on a normal dev box).

### Step 2 (once, ~20-40 min): build the lean Qt

```sh
scripts/build-qt-lean.sh              # clones qtbase, configures, builds, installs
scripts/build-qt-lean.sh --no-ltcg    # ~2x faster build, binary ~1 MB heavier
scripts/build-qt-lean.sh --reconfigure # after changing flags
```

Installs to `~/Qt/6.11.1-lean-static` (override with `QT_LEAN_PREFIX`).
Nothing outside that prefix and the qtbase checkout (`~/src/qtbase`) is
written. Build tree is ~4 GB and can be deleted afterwards
(`~/src/qtbase-build-lean`).

Key `configure` flags and why:

| Flag | Effect |
|---|---|
| `-static` | Qt links into `calendae`; `--gc-sections` (step 3) then drops unused Qt code — the single biggest win, ~21 MB of `libQt6*.so` code pages become ~8 MB linked in |
| `-no-icu` | no bundled ICU: ~2.5 MB resident + a 30 MB data-file mapping gone. Qt's built-in CLDR still formats dates/numbers per locale; only ICU-grade locale-aware **string collation** is lost, which calendae doesn't use |
| `-optimize-size` / `-ltcg` | `-Os` plus link-time optimisation, another ~1 MB |
| `-system-freetype -fontconfig -system-zlib` | keep these as shared `.so` so they stay shared with the rest of the desktop instead of being baked privately into the static blob |
| `-no-opengl -no-feature-vulkan -no-feature-printsupport -no-feature-concurrent -no-feature-sql -no-feature-testlib` | none of these are used by the app |

### Step 3: build calendae against it

```sh
scripts/build-lean.sh          # -> build-lean/calendae
scripts/build-lean.sh --run    # and launch it
```

This sets `-DCALENDAE_BUILD_TRANSLATIONS=OFF`, `-DTINY_GCAL_BUILD_TESTS=OFF`,
`MinSizeRel`, and `-ffunction-sections -fdata-sections` /
`-Wl,--gc-sections` so the linker strips unreferenced Qt code, then
`strip`s the result.

Every Qt upgrade or `build-qt-lean.sh` re-run needs a `build-lean.sh` re-run
too (static linkage).

### Shared lean Qt (what releases ship)

`QT_LINKAGE=shared` on both scripts builds the same stripped-down Qt but
*dynamically* linked, into its own prefix (`~/Qt/$QT_VERSION-lean-shared`)
and build dir (`build-lean-shared/`):

```sh
QT_LINKAGE=shared scripts/build-qt-lean.sh
QT_LINKAGE=shared scripts/build-lean.sh
```

It keeps every memory trim except the `-static` + `--gc-sections` dead-code
drop, so RSS/PSS land between the standard and static-lean numbers. The
reason to ship it rather than the static build: replacing a bundled
`libQt6*.so` is enough to satisfy the LGPL relinking right, so no
object-file "relink kit" has to accompany the download. `build-lean.sh`
also configures `-DCALENDAE_INSTALL_QT_RUNTIME=OFF` here, so `cmake
--install` produces just the binary plus the freedesktop files and the
packaging step (`packaging/linux/`) bundles Qt with `linuxdeploy`.

### Packaging (AppImage + tarball + deb)

`.github/workflows/release.yml` does this on a `v*` tag push; to reproduce
locally after the shared build above:

```sh
packaging/linux/build-appimage.sh build-lean-shared ~/Qt/$QT_VERSION-lean-shared dist
packaging/linux/build-tarball.sh  build-lean-shared/AppDir <version> dist
packaging/linux/build-deb.sh      build-lean-shared/AppDir <version> dist
```

`build-appimage.sh` needs `linuxdeploy` and `linuxdeploy-plugin-qt` on
`PATH` and populates `build-lean-shared/AppDir`, which the other two reuse.
The `.deb` installs the whole bundle under `/usr/lib/calendae` with
`/usr/bin/calendae` a symlink into it, so it carries its own Qt and only
depends on base X/font libraries (`dpkg-shlibdeps`) — it installs on
anything from Ubuntu 22.04 to Debian 13 regardless of their Qt version.

### Flatpak

Unlike the three formats above, the Flatpak build does **not** use the lean
Qt: it links against the Qt already provided by the `org.kde.Platform`
runtime, per normal Flatpak convention (bundling your own Qt inside a
Flatpak alongside the runtime's is both redundant and against Flathub
guidelines). Measured on a real machine this comes to ~130 MB RSS/PSS --
noticeably above even the *standard* build (~68/~45), not just short of the
lean one. Two things stack up: `org.kde.Platform` ships every Qt module
(ICU, OpenGL, SQL, print support, ...) with none of the lean build's
trims, and it pulls in KDE Frameworks for native Plasma theming, which the
other builds never load at all. PSS also can't do its usual job of
discounting shared pages until *other* Flatpak apps on the same
`org.kde.Platform//6.9` are actually running alongside it -- on a machine
with only this one Flatpak app installed, PSS lands right next to RSS.
This is the deliberate trade-off of shipping via Flatpak (sandboxing +
Flathub-style updates) rather than a regression to fix: prefer the
AppImage/tarball/deb above whenever the RAM target is the actual goal.

```sh
flatpak remote-add --if-not-exists --user flathub \
    https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.kde.Platform//6.9 org.kde.Sdk//6.9

packaging/flatpak/build-flatpak.sh <version> dist
flatpak install --user -y --bundle dist/calendae-*.flatpak
flatpak run com.github.frutality.Calendae
```

The manifest (`flatpak/com.github.frutality.Calendae.yml`) builds from the
local checkout (`type: dir`, not a pinned git source) so CI always packages
exactly the commit it just built and tested -- see the comment in the
manifest for what a real Flathub submission would need instead (a pinned
git source for calendae, no `--share=network` at build time, and qtkeychain
vendored as its own source rather than fetched live). `finish-args` grants
network (also needed for the OAuth redirect to reach the sandboxed loopback
server), Wayland/X11, and `--talk-name` for the two D-Bus keyring backends
(Secret Service, KWallet) `src/auth/keychainjob.h` can fall back through.

### Windows

There is no lean Qt for Windows (`scripts/build-qt-lean.sh` is Linux-only),
so the Windows release links the same full, dynamically-linked Qt as the
`build-standard.sh` build -- no separate low-RAM Windows variant exists.
Deployment is a portable zip, not an installer: unzip and run, matching the
AppImage/tarball experience rather than the `.deb`'s (no Start Menu entry,
no uninstaller, no registry writes).

```powershell
pip install aqtinstall==3.3.0
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt -m qttools

packaging\windows\build-zip.ps1 -Version <version> -QtDir C:\Qt\6.8.3\msvc2022_64
```

Needs a Visual Studio "Developer" environment on `PATH` (`cl.exe`) --
`.github/workflows/release.yml` sets this up via `ilammy/msvc-dev-cmd`; a
local run should use "Developer PowerShell for VS" or run
`vcvarsall.bat x64` first. The script runs `windeployqt --compiler-runtime`,
which also copies the MSVC redistributable DLLs (`vcruntime140.dll` etc.)
next to `calendae.exe`, so the zip runs on a clean Windows install without
the user separately installing the Visual C++ Redistributable.

Not code-signed: Windows SmartScreen will show an "unknown publisher"
warning on first run. A real code-signing certificate costs money and
identity verification that's out of scope for this project; users have to
click through it (or `Unblock-File` the zip's contents).

### In CLion

CLion drives CMake through **profiles** (one build dir + option set each).
Add a "Lean" profile alongside the usual Debug/Release ones; after that the
lean build is just a matter of picking that profile from the toolbar and
hitting Build.

1. Run `scripts/build-qt-lean.sh` once from a terminal — CLion can't build
   Qt itself, it only consumes the result in `~/Qt/6.11.1-lean-static`.
2. **Settings → Build, Execution, Deployment → CMake → `+`** to add a
   profile. Set:
   * **Name:** `Lean`
   * **Build type:** `MinSizeRel`
   * **CMake options** (use absolute paths — CLion does not expand `~`):
     ```
     -DCMAKE_PREFIX_PATH=/home/<you>/Qt/6.11.1-lean-static
     -DCALENDAE_BUILD_TRANSLATIONS=OFF
     -DTINY_GCAL_BUILD_TESTS=OFF
     -DFETCHCONTENT_SOURCE_DIR_QTKEYCHAIN=/home/<you>/projects/calendae/build/_deps/qtkeychain-src
     -DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections
     -DCMAKE_EXE_LINKER_FLAGS=-Wl,--gc-sections -Wl,--as-needed
     ```
     `FETCHCONTENT_SOURCE_DIR_QTKEYCHAIN` points at the QtKeychain checkout
     the standard `build/` already fetched, so configuring a fresh profile
     doesn't re-`git clone` it. Without it, CLion's CMake reload runs
     `git clone https://github.com/frankosterfeld/qtkeychain.git` in a
     no-tty context and, if git decides it wants credentials, pops
     `ksshaskpass` asking for a github.com password (which no longer
     exists — GitHub dropped HTTPS password auth in 2021). Anonymous HTTPS
     otherwise works fine; do **not** switch the URL to `git://`, GitHub
     disabled that protocol in 2022.

     If CLion mis-splits the `*_FLAGS` values that contain a space, wrap
     each in quotes: `-DCMAKE_CXX_FLAGS="-ffunction-sections -fdata-sections"`.
     Those two lines are only worth ~1 MB — drop them if they fight the IDE.
   * **Build directory:** `cmake-build-lean` (keep it distinct from the
     other profiles' dirs).
3. Apply. CLion reloads CMake for the new profile. Pick **Lean** in the
   profile/configuration switcher on the toolbar, then Build (`Ctrl+F9`).
   The binary is `cmake-build-lean/calendae`.

Notes:
* The `strip` that `build-lean.sh` does at the end is cosmetic (disk only,
  not RAM) — skip it, or add it as an *After Build* external tool.
* Keep the existing Debug profile as the default for day-to-day work and
  for running tests; the Lean profile builds no tests.
* After a `scripts/build-qt-lean.sh` rerun, do **Tools → CMake → Reset
  Cache and Reload Project** on the Lean profile so it relinks.

### What NOT to do

* **Per-process `FONTCONFIG_FILE`** to shrink the font cache: tried and
  reverted. The system fontconfig caches are *shared* across every GUI app,
  so a private cache is smaller in RSS but **larger in PSS**, and mixing
  font families (Latin from one, Cyrillic from another) looks bad.
* **Lazy week/day view construction:** the widget scaffolding is only ~100
  KB; event pills are already created lazily. Not worth the risk.

---

## Measuring memory

```sh
scripts/measure-memory.sh build-lean/calendae      # 45s settle window
scripts/measure-memory.sh build/calendae 60
```

Reading the output:

* **RSS** (`VmRSS`, and what the in-window indicator historically showed):
  every shared-library page counted in full. Overstates the real cost.
* **PSS** (`/proc/PID/smaps_rollup`): shared pages counted only as this
  process's fraction. **The honest per-process number** and the one the
  30 MB target is measured against. The in-window indicator now shows both
  (`RSS … · PSS …`).
* **Private_Dirty**: memory that is unambiguously only this process's —
  the floor.

---

## Environment variable reference

| Var | Used by | Default |
|---|---|---|
| `QT_VERSION` | all | `6.11.1` |
| `QT_PREFIX` | build-standard | `~/Qt/$QT_VERSION/gcc_64` |
| `QT_LEAN_PREFIX` | build-qt-lean, build-lean | `~/Qt/$QT_VERSION-lean-static` |
| `QT_SRC_DIR` | build-qt-lean | `~/src/qtbase` |
| `QT_BUILD_DIR` | build-qt-lean | `$QT_SRC_DIR-build-lean` |
| `BUILD_DIR` | build-standard, build-lean | `build/` resp. `build-lean/` |
| `JOBS` | all | `nproc` |
