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
