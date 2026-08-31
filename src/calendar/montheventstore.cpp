#include "montheventstore.h"

#include "eventcachestore.h"
#include "eventtimerange.h"
#include "googlecalendarapi.h"
#include "monthgrid.h"
#include "monthkeys.h"

#include <QLocale>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcStore, "tgc.eventstore")

// A loaded bucket older than this is re-fetched on the next ensureMonths(),
// so navigating back to a month visited earlier in the session still picks
// up changes made elsewhere (e.g. an event added in the Google web UI).
constexpr qint64 kBucketFreshnessMs = 5 * 60 * 1000;
} // namespace

MonthEventStore::MonthEventStore(GoogleCalendarApi *calendarApi, EventCacheStore *cache, QObject *parent)
    : QObject(parent)
    , m_calendarApi(calendarApi)
    , m_cache(cache)
{
    // Queued: GoogleCalendarApi::fetchEvents() emits eventsFetchFailed
    // synchronously in the not-signed-in case, which would otherwise land
    // before fetchBucket() has recorded the request id. Real network
    // replies are already async, so this only defers that one edge case by
    // an event-loop turn.
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetched,
            this, &MonthEventStore::onEventsFetched, Qt::QueuedConnection);
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetchFailed,
            this, &MonthEventStore::onEventsFetchFailed, Qt::QueuedConnection);
}

bool MonthEventStore::bucketFresh(const Bucket &bucket) const
{
    return bucket.state == State::Loaded && bucket.fetchedAt.isValid()
        && bucket.fetchedAt.msecsTo(QDateTime::currentDateTimeUtc()) < kBucketFreshnessMs;
}

bool MonthEventStore::bucketNeedsFetch(const Bucket &bucket) const
{
    if (bucket.state == State::InFlight)
        return false;
    // A disk-hydrated bucket is never trusted as fresh until its first
    // server refresh this session — cold start always re-fetches.
    if (bucket.fromDiskPendingRefresh)
        return true;
    return !bucketFresh(bucket);
}

void MonthEventStore::ensureMonths(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds)
{
    for (const QDate &rawMonth : monthKeys) {
        const QDate month = MonthKeys::normalize(rawMonth);
        if (!month.isValid())
            continue;
        for (const QString &calendarId : calendarIds) {
            if (m_buckets[month][calendarId].state == State::NotLoaded)
                tryHydrateFromDisk(month, calendarId); // may emit bucketUpdated

            if (bucketNeedsFetch(m_buckets[month][calendarId]))
                fetchBucket(month, calendarId);
        }
    }
}

void MonthEventStore::refreshVisible(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds)
{
    for (const QDate &rawMonth : monthKeys) {
        const QDate month = MonthKeys::normalize(rawMonth);
        if (!month.isValid())
            continue;
        for (const QString &calendarId : calendarIds) {
            if (m_buckets[month][calendarId].state == State::NotLoaded)
                tryHydrateFromDisk(month, calendarId); // parity with ensureMonths(): paint something first

            if (m_buckets[month][calendarId].state == State::InFlight)
                continue; // a fetch is already on the way; let it land

            // Unlike ensureMonths() there is no bucketNeedsFetch() gate here:
            // the poll's whole job is to re-hit the network past the TTL.
            fetchBucket(month, calendarId);
        }
    }
}

void MonthEventStore::tryHydrateFromDisk(const QDate &month, const QString &calendarId)
{
    if (!m_cache)
        return;
    const std::optional<EventCacheStore::CachedBucket> cached = m_cache->loadBucket(month, calendarId);
    if (!cached)
        return;

    Bucket &bucket = m_buckets[month][calendarId];
    bucket.state = State::Loaded;
    bucket.events = cached->events;
    bucket.fetchedAt = cached->fetchedAt.toUTC();
    bucket.fromDiskPendingRefresh = true;
    qCDebug(lcStore) << "bucket hydrated from disk" << month.toString(Qt::ISODate) << calendarId
                     << cached->events.size() << "events";
    emit bucketUpdated(month, calendarId);
}

void MonthEventStore::fetchBucket(const QDate &month, const QString &calendarId)
{
    const QList<QDate> gridDates = MonthGrid::datesForGrid(month, QLocale::system().firstDayOfWeek());
    const EventTimeRange::Range range = EventTimeRange::forDates(gridDates);

    const quint64 requestId = m_calendarApi->fetchEvents(calendarId, range.timeMin, range.timeMax);
    qCDebug(lcStore) << "fetch bucket" << month.toString(Qt::ISODate) << calendarId << "req" << requestId;

    Bucket &bucket = m_buckets[month][calendarId];
    // Any previous in-flight request for this bucket (a stale-TTL re-fetch)
    // is now superseded — forget its id so its reply is ignored.
    if (bucket.requestId != 0)
        m_bucketByRequestId.remove(bucket.requestId);
    // Keep bucket.events as-is: a disk-hydrated or previously-loaded bucket
    // keeps showing what it has while the refresh is in flight.
    bucket.state = State::InFlight;
    bucket.requestId = requestId;
    m_bucketByRequestId.insert(requestId, {month, calendarId});
}

void MonthEventStore::onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events)
{
    const auto it = m_bucketByRequestId.constFind(requestId);
    if (it == m_bucketByRequestId.constEnd())
        return; // not ours (e.g. ReminderScheduler's), superseded, or invalidated
    const QDate month = it.value().first;
    m_bucketByRequestId.erase(it);

    Bucket &bucket = m_buckets[month][calendarId];
    bucket.state = State::Loaded;
    bucket.events = events;
    qCDebug(lcStore) << "bucket loaded" << month.toString(Qt::ISODate) << calendarId << events.size() << "events";
    bucket.fetchedAt = QDateTime::currentDateTimeUtc();
    bucket.requestId = 0;
    bucket.fromDiskPendingRefresh = false;

    if (m_cache)
        m_cache->storeBucket(month, calendarId, events);

    emit bucketRefreshed(month, calendarId);
    emit bucketUpdated(month, calendarId);
}

void MonthEventStore::onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message, bool transient)
{
    const auto it = m_bucketByRequestId.constFind(requestId);
    if (it == m_bucketByRequestId.constEnd())
        return;
    const QDate month = it.value().first;
    m_bucketByRequestId.erase(it);

    Bucket &bucket = m_buckets[month][calendarId];
    bucket.requestId = 0;

    if (!bucket.events.isEmpty()) {
        // Keep showing what we have (disk-hydrated or an earlier success).
        // Stay Loaded, but mark for another try so the next navigation to
        // this month re-attempts rather than trusting the TTL.
        bucket.state = State::Loaded;
        bucket.fromDiskPendingRefresh = true;
        qCDebug(lcStore) << "bucket refresh failed, keeping stale data" << month.toString(Qt::ISODate)
                         << calendarId << ":" << message;
        emit bucketRefreshFailed(month, calendarId, message, transient);
        return;
    }

    bucket.state = State::Failed;
    bucket.fetchedAt = QDateTime(); // not fresh — next ensureMonths() retries
    emit bucketFetchFailed(month, calendarId, message, transient);
}

bool MonthEventStore::monthsSettled(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds) const
{
    for (const QDate &rawMonth : monthKeys) {
        const QDate month = MonthKeys::normalize(rawMonth);
        if (!month.isValid())
            continue;
        const auto monthIt = m_buckets.constFind(month);
        for (const QString &calendarId : calendarIds) {
            if (monthIt == m_buckets.constEnd())
                return false;
            const auto calIt = monthIt->constFind(calendarId);
            if (calIt == monthIt->constEnd())
                return false;
            if (calIt->state == State::NotLoaded)
                return false;
            // In flight with nothing to show yet — still unsettled. In
            // flight *over* existing events (a refresh) counts as settled:
            // the view already has something.
            if (calIt->state == State::InFlight && calIt->events.isEmpty())
                return false;
        }
    }
    return true;
}

QList<Event> MonthEventStore::eventsFor(const QString &calendarId, const QList<QDate> &monthKeys) const
{
    QList<Event> result;
    QSet<QString> seenIds;
    for (const QDate &rawMonth : monthKeys) {
        const QDate month = MonthKeys::normalize(rawMonth);
        const auto monthIt = m_buckets.constFind(month);
        if (monthIt == m_buckets.constEnd())
            continue;
        const auto calIt = monthIt->constFind(calendarId);
        // Any bucket that has events is worth showing, even one currently
        // refreshing (InFlight) or one that failed its last refresh but
        // still holds disk-hydrated data. A bucket with no events (never
        // loaded, or a hard failure) contributes nothing.
        if (calIt == monthIt->constEnd() || calIt->events.isEmpty())
            continue;
        for (const Event &event : calIt->events) {
            if (seenIds.contains(event.id))
                continue;
            seenIds.insert(event.id);
            result.append(event);
        }
    }
    return result;
}

void MonthEventStore::invalidateCalendar(const QString &calendarId)
{
    for (auto monthIt = m_buckets.begin(); monthIt != m_buckets.end(); ++monthIt) {
        const auto calIt = monthIt->find(calendarId);
        if (calIt == monthIt->end())
            continue;
        if (calIt->requestId != 0)
            m_bucketByRequestId.remove(calIt->requestId);
        monthIt->erase(calIt);
    }
    if (m_cache)
        m_cache->removeCalendar(calendarId);
}

void MonthEventStore::invalidateAll(bool alsoDisk)
{
    m_buckets.clear();
    m_bucketByRequestId.clear();
    if (alsoDisk && m_cache)
        m_cache->removeAll();
}
