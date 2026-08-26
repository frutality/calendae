# Calendae

A lightweight Qt6 desktop client for a single Google Calendar account: sign in, view a month grid, and create/edit/delete events.

Why it exists: I was trying to decrease RAM usage on my somewhat old PC, but every calendar app I tried was using ~150-300 MB and had so many functions I never used. So I figured: why not build it myself?

Currently, with four active calendars, Calendae uses ~35 MB of RAM (PSS) on my machine, or ~60 MB (RSS).

## Requirements

- **CMake** 3.19 or newer
- **A C++17 compiler** — GCC or Clang on Linux, Clang (Xcode Command Line Tools) on macOS, MSVC or MinGW on Windows
- **Qt 6.5+**, with these modules: `Core`, `Widgets`, `Network`, `LinguistTools`, `DBus`
  - `DBus` isn't requested directly by this project's `CMakeLists.txt`, but the bundled [QtKeychain](https://github.com/frankosterfeld/qtkeychain) dependency needs it on Linux (for its KWallet backend), so it must be part of your Qt installation.
- **Git** — required at configure time: CMake's `FetchContent` clones QtKeychain (secure credential storage) from GitHub automatically. An internet connection is needed the first time you configure the project.

Qt can be installed via the [official Qt online installer](https://www.qt.io/download-qt-installer), or through your platform's package manager (see the Linux notes below).

### Linux notes

- Credential storage uses **KWallet via D-Bus**. This build has Secret Service / GNOME Keyring support disabled (`LIBSECRET_SUPPORT OFF` in `CMakeLists.txt`), so a `kwalletd` service needs to be reachable over D-Bus for secure storage to work. Without it, QtKeychain falls back to a less secure plain-text store. If you need GNOME Keyring support instead, install `libsecret-1-dev` and flip `LIBSECRET_SUPPORT` to `ON` in `CMakeLists.txt`.
- Example dependency install on Ubuntu/Debian (adjust for your distro):
  ```
  sudo apt install build-essential cmake git qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-base-dev-tools libqt6dbus6-dev
  ```

### macOS notes

- Install Xcode Command Line Tools: `xcode-select --install`
- Credential storage uses the native macOS Keychain automatically — no extra system packages needed.
- If Qt was installed via the Qt online installer rather than Homebrew, point `CMAKE_PREFIX_PATH` at it (see below).

### Windows notes

- Use either the MSVC toolchain (Visual Studio Build Tools) or MinGW, matching the Qt kit you installed.
- Credential storage uses the native Windows Credential Manager automatically — no extra system packages needed.
- Run the build commands from a shell that has your compiler on `PATH` (e.g. the "Developer Command Prompt for VS" for MSVC).

## Building

These are native build instructions: run the Linux steps on Linux, the macOS steps on macOS, and the Windows steps on Windows. There's no cross-compilation setup here — you can't build the Windows binary from a Linux machine (or vice versa) with these commands.

From the project root:

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/your/Qt/6.x.y/gcc_64
cmake --build build --parallel
```

Replace the `CMAKE_PREFIX_PATH` value with your own Qt installation's platform directory, for example:

- Linux: `~/Qt/6.5.0/gcc_64` (or omit `CMAKE_PREFIX_PATH` entirely if Qt was installed via your Linux distro's package manager and is already on CMake's default search path)
- macOS: `~/Qt/6.5.0/macos`
- Windows (MSVC): `C:\Qt\6.5.0\msvc2019_64`
- Windows (MinGW): `C:\Qt\6.5.0\mingw_64`

Extra `-D...` options (like the two below) are CMake cache variables, read at *configure* time — they go on the **first** command, not the second:

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/your/Qt/6.x.y/gcc_64 -DCMAKE_BUILD_TYPE=Release -DTINY_GCAL_BUILD_TESTS=OFF
cmake --build build --parallel
```

- To build a Release configuration instead of the CMake default, add `-DCMAKE_BUILD_TYPE=Release` (single-configuration generators like Makefiles/Ninja) or pass `--config Release` to the *build* command instead (multi-configuration generators like Visual Studio, which ignore `CMAKE_BUILD_TYPE`).
- Unit tests are built by default. To skip them, add `-DTINY_GCAL_BUILD_TESTS=OFF`.

If you change one of these options later, re-run the first (configure) command with the new flag — CMake will pick it up without needing a clean rebuild.

## Running

The built binary lands under the `build` directory:

- Linux: `build/calendae`
- macOS: `build/calendae.app` (an app bundle — launch with `open build/calendae.app`, or run the executable inside it directly)
- Windows: `build\calendae.exe` (or `build\Debug\calendae.exe` / `build\Release\calendae.exe` with a multi-configuration generator like Visual Studio)

## Running tests

```
cd build
ctest --output-on-failure
```

## First run: Google OAuth credentials

This app doesn't ship with a Google OAuth client — you need your own "Desktop app" OAuth client ID and secret from the [Google Cloud Console](https://console.cloud.google.com/). On first sign-in, the app prompts for these and securely saves them (via the OS credential store described above) for future runs. Alternatively, you can pre-supply them in a JSON file at your OS's standard app config location (e.g. `~/.config/calendae/oauth_client.json` on Linux) with `client_id` and `client_secret` fields.
