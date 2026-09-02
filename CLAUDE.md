# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Calendae: a lightweight Qt6 desktop client for a single Google Calendar account (sign in, view a month grid, create/edit/delete events). Built specifically to be light on RAM compared to full-featured calendar apps — keep that goal in mind when adding dependencies or features; don't add functionality nobody asked for.

## Commands

Configure + build (from project root):
```
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/your/Qt/6.x.y/gcc_64
cmake --build build --parallel
```
Omit `CMAKE_PREFIX_PATH` if Qt is on CMake's default search path (e.g. installed via a Linux distro package manager). Requires Qt 6.5+ (Core, Widgets, Network, LinguistTools, DBus) and Git (QtKeychain is fetched via `FetchContent` at configure time).

Useful configure-time flags (re-run the configure command, not the build command, to change these):
- `-DCMAKE_BUILD_TYPE=Release` — single-configuration generators (Makefiles/Ninja); use `--config Release` on the *build* command instead for multi-config generators (Visual Studio).
- `-DTINY_GCAL_BUILD_TESTS=OFF` — skip building the unit tests (on by default).

Run the app: `build/calendae` (Linux/Windows) or `open build/calendae.app` (macOS).

Run all tests:
```
cd build
ctest --output-on-failure
```

Run a single test: `ctest --test-dir build -R test_pkce --output-on-failure`, or run the test binary directly (`build/tests/test_pkce`). Test targets are named `test_<name>` (see `tests/CMakeLists.txt` for the full list); each links directly against `tgc_calendar` (and transitively `tgc_auth`), not the `calendae` executable, so pure-logic code is testable without constructing `MainWindow`.

## Architecture

The CMake structure enforces a three-layer split — respect it when adding new files:
- **`tgc_auth`** (static lib, `src/auth/`): Google OAuth 2.0 PKCE flow and credential storage. Knows nothing about the Calendar API.
- **`tgc_calendar`** (static lib, `src/calendar/`, depends on `tgc_auth`): Google Calendar REST wrapper plus the month-grid UI widgets and event dialogs.
- **`calendae`** executable (`src/app/`): `MainWindow` only. It holds no business logic beyond wiring signals/slots between the pieces below, tracking the most recent `calendarListFetched` result, and generating request ids for pending create/update/delete calls.

**Auth flow** (`AuthManager`, the sole orchestrator): `SignedOut` → `Restoring` (silent restore from a stored refresh token at startup) or `SigningIn` (interactive RFC 8252 loopback flow: opens the system browser, `OAuthLoopbackServer` catches the redirect on `127.0.0.1`) → `SignedIn`. OAuth client id/secret resolution goes through `CredentialsProvider`'s fallback chain: config file → QtKeychain → first-run dialog (`OAuthCredentialsDialog`), saved back to the keychain afterward. `AuthManager` never calls the Calendar API itself; `GoogleCalendarApi` reads the bearer token from it on every request instead of caching/refreshing tokens on its own (a 401 is treated as a hard, user-visible error — proactive refresh 60s before expiry is what's supposed to prevent that).

**Event/calendar data flow**: `GoogleCalendarApi` is a thin, stateless-per-call REST wrapper. `MonthEventStore` is one shared, month-granularity cache sitting between it and the three view controllers: its unit is a "bucket" = one `(calendar month, calendarId)` pair, fetched over that month's full 42-day grid range. `ensureMonths()` starts a request per not-already-fresh bucket; `eventsFor()` returns the union (deduped by event id) of the loaded buckets a view needs. Because the store is shared, switching month→week→day within an already-fetched month — and paging within it — costs zero network round-trips; only landing on a not-yet-cached month (or a stale one, past the per-bucket freshness TTL) fetches. `MonthEventsController` / `TimeGridEventsController` are now thin: they own which calendars are enabled, ask the store to keep the visible month(s) loaded (`MonthEventsController` deliberately asks for just the displayed month, not all 3 the grid touches — the bucket's 42-day grid range already covers the fringe), re-render from `eventsFor()` on each `bucketUpdated`, group into `MonthDayEventItem` lists, and emit `fetchCycleFinished` once `monthsSettled()` (which can be synchronous on a full cache hit). `ReminderScheduler` keeps its own independent rolling fetch and does NOT use the store. `MainWindow` wires everything via `connect()` and, on an event mutation, calls `MonthEventStore::invalidateCalendar()` once before fanning `refreshCalendar()` out to the controllers.

**Disk cache (`EventCacheStore`)**: an optional on-disk mirror injected into `MonthEventStore`, under `<CacheLocation>/events/<accountKey>/` (one CBOR file per bucket, plus `calendars.cbor` for the last calendar list). A `NotLoaded` bucket is first hydrated from disk — `bucketUpdated` fires synchronously so a cold start / offline launch paints last-known events immediately — then still refreshed from the network: a disk-hydrated bucket is flagged `fromDiskPendingRefresh` so its first refresh runs regardless of the freshness TTL ("just restarted" never means trusting stale data). Successful fetches write back (skipped when serialized bytes are unchanged, so paging doesn't churn the disk); `mtime` is the "fetched at". A failed refresh over data already on screen emits `bucketRefreshFailed` (not `bucketFetchFailed`) and keeps the stale events. `accountKey` is `AuthManager::accountKey()` (SHA-256 of the refresh token); explicit sign-out calls `invalidateAll(alsoDisk=true)` and wipes it. `GoogleCalendarApi`'s `eventsFetchFailed`/`calendarListFetchFailed` carry a `transient` flag (`isTransientNetworkError`: connectivity failures, never an HTTP status ≥ 400); a transient failure puts `MainWindow` into a "Google Calendar unavailable — showing saved data" state with a 90s `fetchCalendarList()` retry, cleared on the next success (`bucketRefreshed` or `calendarListFetched`). `ReminderScheduler` has no disk cache and is not part of the offline indicator (only nudged via `refreshAll()` on reconnect).

**Recurring patterns to follow when extending this code:**
- *Optimistic UI + staleness filtering*: UI-affecting actions (sidebar toggles, event create/update/delete) update the UI immediately and roll back via a `*Failed` signal on error. Every async request carries a caller-generated opaque `requestId`, echoed back unchanged in the response signal, so a late reply for an obsolete request (calendar deselected, month navigated away, dialog closed) gets detected and dropped rather than applied.
- *Data structs are plain and parse themselves*: `Calendar`, `Event`, `NewEventRequest`, `TokenResponse` are QObject-free structs with static `fromJson`/`listFromJson` parsers, kept separate from the classes that do networking so JSON parsing is unit-testable in isolation. `Event`/`Calendar` additionally have `toCbor`/`fromCbor` for the disk cache — a deliberately separate serialization from the Google wire format so the two evolve independently (`EventCacheStore` bumps `kSchemaVersion` and discards older files).
- *All-day vs timed* events use one struct with an `allDay` bool gating which of two field pairs is valid (`QDate` pair vs. `QDateTime` pair) — this convention repeats in `Event` and `NewEventRequest`; follow it rather than introducing a different representation.
- Only one signed-in account and one window are supported — there's no multi-account/multi-window state to preserve when touching auth or the main window.

**Qt-specific setup:** `qt_standard_project_setup()` plus `qt_add_executable`/`qt_add_translations` (Qt6 CMake API) handle `WIN32`/`MACOSX_BUNDLE` flags, the deploy script, and `.ts` → `.qm` translation compilation — don't hand-roll any of that. QtKeychain is vendored via `FetchContent` (pinned to `v0.14.0`, not a system dependency), built as a static lib. Its Linux secret storage is layered (see `src/auth/keychainjob.h`, which is the single construction point for every keychain job): the Secret Service/libsecret backend (any `org.freedesktop.secrets` provider — gnome-keyring, KeePassXC, KWallet 6, …) is compiled in when the `libsecret-1` headers are present at configure time (auto-detected; force with `-DCALENDAE_WITH_LIBSECRET=ON/OFF`), with KWallet's own D-Bus protocol next, and finally `setInsecureFallback(true)` persisting to a plain-text `QSettings` file so a keyring-less machine still keeps its session across restarts instead of forcing a browser sign-in every launch. `keychainSecretStoredInsecurely()` reports when that last tier was used; `MainWindow` shows a one-time status-bar warning.

## First run / OAuth credentials

This app doesn't ship a Google OAuth client. On first sign-in it prompts for a "Desktop app" client id/secret from the Google Cloud Console and stores them via the OS credential store; alternatively they can be pre-supplied as JSON (`client_id`, `client_secret`) at the OS's standard app config location (e.g. `~/.config/calendae/oauth_client.json` on Linux).
