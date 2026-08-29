#include "montheventstore.h"

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

MonthEventStore::MonthEventStore(GoogleCalendarApi *calendarApi, QObject *parent)
    : QObject(parent)
    , m_calendarApi(calendarApi)
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

void MonthEventStore::ensureMonths(const QList<QDate> &monthKeys, const QSet<QString> &calendarIds)
{
    for (const QDate &rawMonth : monthKeys) {
        const QDate month = MonthKeys::normalize(rawMonth);
        if (!month.isValid())
            continue;
        for (const QString &calendarId : calendarIds) {
            const Bucket &bucket = m_buckets[month][calendarId];
            if (bucket.state == State::InFlight || bucketFresh(bucket))
                continue;
            fetchBucket(month, calendarId);
        }
    }
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

    emit bucketUpdated(month, calendarId);
}

void MonthEventStore::onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message)
{
    const auto it = m_bucketByRequestId.constFind(requestId);
    if (it == m_bucketByRequestId.constEnd())
        return;
    const QDate month = it.value().first;
    m_bucketByRequestId.erase(it);

    Bucket &bucket = m_buckets[month][calendarId];
    bucket.state = State::Failed;
    bucket.fetchedAt = QDateTime(); // not fresh — next ensureMonths() retries
    bucket.requestId = 0;

    emit bucketFetchFailed(month, calendarId, message);
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
            if (calIt->state == State::NotLoaded || calIt->state == State::InFlight)
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
        if (calIt == monthIt->constEnd() || calIt->state != State::Loaded)
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
}

void MonthEventStore::invalidateAll()
{
    m_buckets.clear();
    m_bucketByRequestId.clear();
}
