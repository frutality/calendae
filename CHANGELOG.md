# Changelog

All notable changes to Calendae are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the version is `0.x`, anything may change on a minor bump.

## [0.2.0] - 2026-09-21

### Fixed
- After the PC woke from a long suspend, calendars stopped syncing with a
  "session may have expired" error until the app was restarted. An expired
  access token is now renewed automatically and the request retried; only a
  refresh token that Google actually rejects requires signing in again.

## [0.1.0] - 2026-09-13

### Added
- First usable version
