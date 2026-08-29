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
// No eviction: buckets accumulate for the session (a per-bucket freshness
// TTL still forces a re-fetch when a stale month is revisited). Bounding
// total memory is left to a later, separate change.
class MonthEventStore : public QObject
{
    Q_OBJECT
public:
    explicit MonthEventStore(GoogleCalendarApi *calendarApi, QObject *parent = nullptr);

    // Start a fetch for every (monthKey, calendarId) pair that isn't already
    // in flight or loaded-and-fresh. monthKeys are normalized to
    // first-of-month internally; order and duplicates don't matter. Cheap
    // and idempotent — safe to call on every navigation.
    void ensureMonths(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds);

    // True when a fresh ensureMonths(monthKeys, calendarIds) would start no
    // new requests: every pair has settled (loaded, or failed permanently
    // until the next ensureMonths) and nothing is in flight. Lets a
    // controller fire fetchCycleFinished, including synchronously on a full
    // cache hit.
    bool monthsSettled(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds) const;

    // Union of cached events for calendarId across the given months,
    // deduplicated by event id. A month whose bucket hasn't loaded (or has
    // failed) contributes nothing.
    QList<Event> eventsFor(const QString &calendarId, const QList<QDate> &monthKeys) const;

    // Drop every bucket for calendarId (all months). Use after an
    // out-of-band mutation of that calendar (event create / update /
    // delete); callers then re-ensure the months their views need.
    void invalidateCalendar(const QString &calendarId);

    // Drop everything (sign-out).
    void invalidateAll();

signals:
    void bucketUpdated(const QDate &monthKey, const QString &calendarId);
    void bucketFetchFailed(const QDate &monthKey, const QString &calendarId, const QString &message);

private slots:
    void onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events);
    void onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message);

private:
    enum class State { NotLoaded, InFlight, Loaded, Failed };

    struct Bucket
    {
        State state = State::NotLoaded;
        QList<Event> events;
        QDateTime fetchedAt;   // when `events` landed; drives the freshness TTL
        quint64 requestId = 0; // newest in-flight / last fetch; keyed in m_bucketByRequestId
    };

    bool bucketFresh(const Bucket &bucket) const;
    void fetchBucket(const QDate &month, const QString &calendarId);

    GoogleCalendarApi *m_calendarApi;
    QHash<QDate, QHash<QString, Bucket>> m_buckets; // [monthKey][calendarId]
    QHash<quint64, QPair<QDate, QString>> m_bucketByRequestId;
};

#endif // MONTHEVENTSTORE_H
