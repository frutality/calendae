#ifndef MONTHEVENTSTORE_H
#define MONTHEVENTSTORE_H

#include "event.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

class EventCacheStore;
class GoogleCalendarApi;

// One shared, month-granularity cache of events, sitting between
// GoogleCalendarApi and the three view controllers (month / week / day).
//
// The unit of fetching and caching is one (calendar month, calendarId)
// pair — a "bucket". A bucket's events are fetched over the *42-day grid* of
// that month (the same [timeMin, timeMax) the month view has always used),
// so any week or day fully inside that month is served from the one bucket
// with no extra request; a week straddling a month boundary is served from
// two adjacent buckets, deduplicated by event id.
//
// Because a single MonthEventStore is shared by all three views, switching
// month -> week -> day within an already-fetched month, and paging within
// it, cost zero network round-trips. Staleness of a slow reply arriving
// after navigation / sign-out / calendar toggle is handled here (per-bucket
// request id), so the controllers no longer each re-implement it.
//
// If an EventCacheStore is supplied, a NotLoaded bucket is first hydrated
// from disk (instant paint on a cold start / while offline) and then still
// refreshed from the network — the first refresh of a disk-hydrated bucket
// always runs regardless of the freshness TTL, so "the app was restarted 20
// seconds ago" never means stale data is trusted. Successful fetches are
// written back to disk.
//
// No in-memory eviction: buckets accumulate for the session (a per-bucket
// freshness TTL still forces a re-fetch when a stale month is revisited).
class MonthEventStore : public QObject
{
    Q_OBJECT
public:
    explicit MonthEventStore(GoogleCalendarApi *calendarApi, EventCacheStore *cache = nullptr,
                              QObject *parent = nullptr);

    // Start a fetch for every (monthKey, calendarId) pair that isn't already
    // in flight or loaded-and-fresh. A pair with nothing in memory is first
    // hydrated from the disk cache (emitting bucketUpdated synchronously) so
    // there's something to render immediately. monthKeys are normalized to
    // first-of-month internally; order and duplicates don't matter. Cheap
    // and idempotent — safe to call on every navigation.
    void ensureMonths(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds);

    // Like ensureMonths(), but for MainWindow's periodic safety poll: forces
    // a server re-fetch of every listed (monthKey, calendarId) pair
    // regardless of its freshness TTL (a bucket refreshed a minute ago is
    // still re-hit), skipping only pairs with a request already in flight.
    // Deliberately decoupled from the lazy freshness TTL so the poll cadence
    // is the only thing that governs how often idle views re-sync. Does not
    // touch bucket.events — the view keeps showing current data until each
    // reply lands via bucketUpdated.
    void refreshVisible(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds);

    // True when every requested pair has something to show or nothing more
    // coming: loaded, failed-permanently-until-the-next-ensureMonths, or
    // refreshing-in-place over data already on screen (disk-hydrated or a
    // prior success). Lets a controller fire fetchCycleFinished, including
    // synchronously on a cache hit.
    bool monthsSettled(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds) const;

    // Union of cached events for calendarId across the given months,
    // deduplicated by event id. A month with no loaded events (never
    // fetched, or a hard failure with no disk fallback) contributes
    // nothing; a month refreshing over previously-shown events still
    // contributes those events.
    QList<Event> eventsFor(const QString &calendarId, const QList<QDate> &monthKeys) const;

    // Drop every bucket for calendarId (all months), in memory and on disk.
    // Use after an out-of-band mutation of that calendar (event create /
    // update / delete); callers then re-ensure the months their views need.
    void invalidateCalendar(const QString &calendarId);

    // Drop every in-memory bucket (sign-out / account change). alsoDisk
    // additionally wipes this account's on-disk cache — pass it on an
    // explicit sign-out, not on a silent teardown you want the next cold
    // start to benefit from.
    void invalidateAll(bool alsoDisk = false);

signals:
    // Bucket contents changed and views should re-render — fires both on a
    // disk hydration and on a server reply, so it is NOT proof of
    // connectivity.
    void bucketUpdated(const QDate &monthKey, const QString &calendarId);
    // A server reply for this bucket just landed successfully — unlike
    // bucketUpdated this does prove the server is reachable.
    void bucketRefreshed(const QDate &monthKey, const QString &calendarId);
    // Fetch failed and there is nothing to show for this bucket.
    void bucketFetchFailed(const QDate &monthKey, const QString &calendarId, const QString &message, bool transient);
    // Fetch failed but previously-loaded events (disk-hydrated or an earlier
    // success) are still on screen — a softer condition than
    // bucketFetchFailed.
    void bucketRefreshFailed(const QDate &monthKey, const QString &calendarId, const QString &message, bool transient);

private slots:
    void onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events);
    void onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message, bool transient);

private:
    enum class State { NotLoaded, InFlight, Loaded, Failed };

    struct Bucket
    {
        State state = State::NotLoaded;
        QList<Event> events;
        QDateTime fetchedAt;   // when `events` landed; drives the freshness TTL
        quint64 requestId = 0; // newest in-flight / last fetch; keyed in m_bucketByRequestId
        // Hydrated from disk this session and not yet confirmed against the
        // server: forces the next ensureMonths() to fetch even if fetchedAt
        // is within the TTL. Cleared on the first successful fetch.
        bool fromDiskPendingRefresh = false;
    };

    bool bucketFresh(const Bucket &bucket) const;
    bool bucketNeedsFetch(const Bucket &bucket) const;
    void tryHydrateFromDisk(const QDate &month, const QString &calendarId);
    void fetchBucket(const QDate &month, const QString &calendarId);

    GoogleCalendarApi *m_calendarApi;
    EventCacheStore *m_cache;
    QHash<QDate, QHash<QString, Bucket>> m_buckets; // [monthKey][calendarId]
    QHash<quint64, QPair<QDate, QString>> m_bucketByRequestId;
};

#endif // MONTHEVENTSTORE_H
