# Changelog

All notable changes to Calendae are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the version is `0.x`, anything may change on a minor bump.

## [Unreleased]

### Added
- Project version resolved by CMake from the git tag (`git describe` fallback,
  `-DCALENDAE_VERSION=` override for CI) and exposed as `calendae --version`.
- `CALENDAE_APP_ID` (`com.github.frutality.Calendae`): the installed
  `.desktop` entry, hicolor icons and a new AppStream `metainfo.xml` are all
  generated from templates and named after it.
- `scripts/lint-metadata.sh` plus a `metadata_lint` CTest that validate the
  generated `.desktop` and metainfo files.
- `LICENSE` (MIT), installed to `share/doc/calendae/`.
- Linux release pipeline (`.github/workflows/release.yml`): a tag push builds
  an AppImage, a portable tarball and a self-contained `.deb`, and opens a
  draft GitHub release.
- `packaging/linux/build-deb.sh` — bundles Qt under `/usr/lib/calendae`
  (so the package is independent of the distro Qt version), `dpkg-shlibdeps`
  for the base-library depends, dpkg triggers for the desktop/icon caches.
- `QT_LINKAGE=shared` in `scripts/build-qt-lean.sh` / `scripts/build-lean.sh`
  for a dynamically-linked lean Qt (keeps the memory trims; Qt stays
  replaceable `.so` so LGPL relinking is trivial). Static stays the default.
- `packaging/linux/build-appimage.sh`, `packaging/linux/build-tarball.sh`,
  `packaging/changelog-extract.sh`.
- `CALENDAE_INSTALL_QT_RUNTIME` CMake option (default ON) — off for packaging
  builds that bundle Qt themselves.
- Flatpak packaging: `flatpak/com.github.frutality.Calendae.yml` (builds
  against the `org.kde.Platform//6.9` runtime, not the lean Qt) and
  `packaging/flatpak/build-flatpak.sh`, producing a single-file `.flatpak`
  bundle. Wired into `release.yml` as its own job feeding the same draft
  release as the other Linux artifacts.
- Windows packaging: `packaging/windows/build-zip.ps1` builds against Qt
  installed via `aqtinstall` and runs `windeployqt --compiler-runtime` to
  produce a self-contained, unzip-and-run portable build (no installer, not
  code-signed). A Windows `.rc` resource (`resources/calendae.rc.in`) now
  embeds a version resource and `resources/icons/calendae.ico` as the
  `.exe` icon. Wired into `release.yml` as its own job (`windows-2022`
  runner) feeding the same draft release.
- macOS packaging: `packaging/macos/build-dmg.sh` builds a universal
  (arm64 + x86_64) `.app` against Qt from `aqtinstall`, folds Qt in with
  `macdeployqt`, ad-hoc codesigns it, and wraps it in a `.dmg`. A generated
  `resources/Info.plist.in` (bundle id = `CALENDAE_APP_ID`, versions from
  the tag) and `resources/icons/calendae.icns` are bundled by CMake. Own
  `release.yml` job on `macos-14`. Not notarized -- Gatekeeper still
  quarantines a downloaded copy.
