# Calendae

A lightweight Qt6 desktop client for a single Google Calendar account: sign in, view a month grid, and create/edit/delete events.

Why it exists: I was trying to decrease RAM usage on my somewhat old PC, but every calendar app I tried was using ~150-300 MB and had so many functions I never used. So I figured: why not build it myself?

Currently, with four active calendars, Calendae uses ~30 MB of RAM (PSS) on my machine, or ~50 MB (RSS) — built as the low-RAM variant described in [`docs/BUILDING.md`](docs/BUILDING.md). A standard build is roughly ~45 MB PSS / ~68 MB RSS.

## Requirements

- **CMake** 3.19 or newer
- **A C++17 compiler** — GCC or Clang on Linux, Clang (Xcode Command Line Tools) on macOS, MSVC or MinGW on Windows
- **Qt 6.5+**, with these modules: `Core`, `Widgets`, `Network`, `DBus`, and `LinguistTools`
  - The 6.5 minimum is real — the date/time handling uses `QTimeZone::LocalTime`, added in Qt 6.5.
  - `DBus` is used for desktop notifications and, on Linux, by the bundled [QtKeychain](https://github.com/frankosterfeld/qtkeychain) dependency (its KWallet backend).
  - `LinguistTools` can be dropped with `-DCALENDAE_BUILD_TRANSLATIONS=OFF` (the app then shows its source strings, which are English); useful for a minimal Qt that doesn't ship it.
- **Git** — required at configure time: CMake's `FetchContent` clones QtKeychain (secure credential storage) from GitHub automatically. An internet connection is needed the first time you configure the project.

Qt can be installed via the [official Qt online installer](https://www.qt.io/download-qt-installer), or through your platform's package manager (see the Linux notes below).

### Linux notes

- Credential storage tries three backends in order: **Secret Service** (any `org.freedesktop.secrets` provider — GNOME Keyring, KeePassXC, KWallet 6, `pass`, …), then **KWallet's own D-Bus protocol**, then a plain-text on-disk file as a last resort (the app shows a one-time warning when it lands there).
  - The Secret Service backend is compiled in only if the `libsecret-1` headers are present at configure time. It's auto-detected; force it with `-DCALENDAE_WITH_LIBSECRET=ON` (or `=OFF`). At runtime the library is `dlopen`ed, so it stays a soft dependency.
- Example dependency install on Ubuntu/Debian (adjust for your distro):
  ```
  sudo apt install build-essential cmake git \
      qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-base-dev-tools \
      libsecret-1-dev
  ```
  `libsecret-1-dev` is optional — omit it to build without the Secret Service backend (KWallet + on-disk fallback still work). QtDBus dev files come with `qt6-base-dev`.

### macOS notes

- Install Xcode Command Line Tools: `xcode-select --install`
- Credential storage uses the native macOS Keychain automatically — no extra system packages needed.
- If Qt was installed via the Qt online installer rather than Homebrew, point `CMAKE_PREFIX_PATH` at it (see below).

### Windows notes

- Use either the MSVC toolchain (Visual Studio Build Tools) or MinGW, matching the Qt kit you installed.
- Credential storage uses the native Windows Credential Manager automatically — no extra system packages needed.
- Run the build commands from a shell that has your compiler on `PATH` (e.g. the "Developer Command Prompt for VS" for MSVC).

## Building

Native builds only — run each platform's steps on that platform, there's no cross-compilation setup here.

**Linux, scripted:** `./scripts/build-standard.sh` runs configure + build + the test suite. It looks for Qt under `~/Qt/<version>/gcc_64`; override with `QT_PREFIX=/path/to/qt` (use `QT_PREFIX=/usr` for a distro Qt). [`docs/BUILDING.md`](docs/BUILDING.md) documents the script options, CLion setup, and a memory-optimised build variant (calendae linked against a stripped static Qt — roughly 30% lower RAM use).

**Manual (any platform)** — from the project root:

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/your/Qt/6.x.y/gcc_64
cmake --build build --parallel
```

Replace `CMAKE_PREFIX_PATH` with your Qt installation's platform directory:

- Linux: `~/Qt/6.5.0/gcc_64` (or omit `CMAKE_PREFIX_PATH` entirely if Qt came from your distro's package manager and is already on CMake's search path)
- macOS: `~/Qt/6.5.0/macos`
- Windows (MSVC): `C:\Qt\6.5.0\msvc2019_64` &nbsp;·&nbsp; (MinGW): `C:\Qt\6.5.0\mingw_64`

Common configure-time options — add them to the **first** command (re-run it to change them later):

- `-DCMAKE_BUILD_TYPE=Release` — optimized build (single-config generators; on Visual Studio pass `--config Release` to the *build* command instead)
- `-DTINY_GCAL_BUILD_TESTS=OFF` — skip the unit tests (built by default)
- `-DCALENDAE_BUILD_TRANSLATIONS=OFF` — skip `.qm` compilation and the LinguistTools requirement

## Running

The built binary lands under the `build` directory:

- Linux: `build/calendae`
- macOS: `build/calendae.app` (an app bundle — launch with `open build/calendae.app`, or run the executable inside it directly)
- Windows: `build\calendae.exe` (or `build\Debug\calendae.exe` / `build\Release\calendae.exe` with a multi-configuration generator like Visual Studio)

## Running tests

`scripts/build-standard.sh` runs the suite after building. To run it on its own against an existing build:

```
ctest --test-dir build --output-on-failure
```

## First run: Google OAuth credentials

This app doesn't ship with a Google OAuth client — you need your own "Desktop app" OAuth client ID and secret from the [Google Cloud Console](https://console.cloud.google.com/). On first sign-in, the app prompts for these and securely saves them (via the OS credential store described above) for future runs. Alternatively, you can pre-supply them in a JSON file at your OS's standard app config location (e.g. `~/.config/calendae/oauth_client.json` on Linux) with `client_id` and `client_secret` fields.
