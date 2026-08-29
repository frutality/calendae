#include "reminderscheduler.h"

#include "auth/authmanager.h"
#include "googlecalendarapi.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QTimer>

#include <limits>

namespace {
Q_LOGGING_CATEGORY(lcReminder, "tgc.reminder")

// How far ahead to keep events fetched. Must comfortably exceed the largest
// reminder lead time the dialog offers (1 day) plus slack for the refresh
// interval.
constexpr int kLookaheadHours = 36;
constexpr int kRefreshIntervalMs = 10 * 60 * 1000; // re-fetch to roll the window forward
constexpr qint64 kMinRefreshGapMs = 30 * 1000;     // floor between refreshAll()-driven cycles
} // namespace

ReminderScheduler::ReminderScheduler(AuthManager *authManager, GoogleCalendarApi *calendarApi, QObject *parent)
    : EventsController(parent)
    , m_authManager(authManager)
    , m_calendarApi(calendarApi)
    , m_refreshTimer(new QTimer(this))
{
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetched, this, &ReminderScheduler::onEventsFetched);
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetchFailed, this, &ReminderScheduler::onEventsFetchFailed);

    m_refreshTimer->setInterval(kRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &ReminderScheduler::startFetchCycle);
}

void ReminderScheduler::setCalendars(const QList<Calendar> &calendars)
{
    m_calendarsById.clear();
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (!m_enabledCalendarIds.contains(calendar.id) && calendar.selected)
            m_enabledCalendarIds.insert(calendar.id);
    }
    // Drop enabled ids for calendars the account no longer has.
    for (auto it = m_enabledCalendarIds.begin(); it != m_enabledCalendarIds.end();) {
        if (m_calendarsById.contains(*it))
            ++it;
        else
            it = m_enabledCalendarIds.erase(it);
    }

    startFetchCycle();
}

void ReminderScheduler::setCalendarEnabled(const QString &calendarId, bool enabled)
{
    if (!m_calendarsById.contains(calendarId))
        return;

    if (enabled) {
        m_enabledCalendarIds.insert(calendarId);
        fetchForCalendar(calendarId);
    } else {
        m_enabledCalendarIds.remove(calendarId);
        m_upcomingByCalendar.remove(calendarId);
        m_latestRequestIdByCalendar.remove(calendarId);
        rebuildTimers();
    }
}

void ReminderScheduler::clear()
{
    m_calendarsById.clear();
    m_enabledCalendarIds.clear();
    m_upcomingByCalendar.clear();
    m_latestRequestIdByCalendar.clear();
    m_firedKeys.clear();
    m_refreshTimer->stop();
    rebuildTimers(); // kills all pending timers
}

void ReminderScheduler::refreshCalendar(const QString &calendarId)
{
    if (!m_calendarsById.contains(calendarId))
        return;

    m_upcomingByCalendar.remove(calendarId);
    if (m_enabledCalendarIds.contains(calendarId))
        fetchForCalendar(calendarId);
    else
        rebuildTimers();
}

std::optional<Event> ReminderScheduler::findCachedEvent(const QString &calendarId, const QString &eventId) const
{
    const auto it = m_upcomingByCalendar.constFind(calendarId);
    if (it == m_upcomingByCalendar.constEnd())
        return std::nullopt;
    for (const Event &event : it.value()) {
        if (event.id == eventId)
            return event;
    }
    return std::nullopt;
}

void ReminderScheduler::refreshAll()
{
    if (m_sinceLastCycle.isValid() && m_sinceLastCycle.elapsed() < kMinRefreshGapMs)
        return; // a cycle ran very recently; let it stand
    startFetchCycle();
}

void ReminderScheduler::startFetchCycle()
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn)
        return;
    if (m_calendarsById.isEmpty())
        return;

    m_sinceLastCycle.restart();
    qCDebug(lcReminder) << "fetch cycle: calendars=" << m_calendarsById.size()
                        << "enabled=" << m_enabledCalendarIds.size();
    for (const QString &calendarId : std::as_const(m_enabledCalendarIds))
        fetchForCalendar(calendarId);

    m_refreshTimer->start(); // restart the 10-minute window-roll countdown
}

void ReminderScheduler::fetchForCalendar(const QString &calendarId)
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn)
        return;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString timeMin = nowUtc.toString(Qt::ISODate);
    const QString timeMax = nowUtc.addSecs(qint64(kLookaheadHours) * 3600).toString(Qt::ISODate);

    const quint64 requestId = m_calendarApi->fetchEvents(calendarId, timeMin, timeMax);
    m_latestRequestIdByCalendar.insert(calendarId, requestId);
}

void ReminderScheduler::onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events)
{
    if (m_latestRequestIdByCalendar.value(calendarId) != requestId)
        return; // superseded or not ours
    if (!m_enabledCalendarIds.contains(calendarId))
        return; // calendar disabled before the reply landed

    qCDebug(lcReminder) << "fetched" << events.size() << "upcoming events for" << calendarId;
    m_upcomingByCalendar.insert(calendarId, events);
    rebuildTimers();
}

void ReminderScheduler::onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message, bool transient)
{
    Q_UNUSED(transient); // reminder fetches stay silent regardless of the failure kind
    if (m_latestRequestIdByCalendar.value(calendarId) != requestId)
        return;
    // A background reminder fetch failing must not nag the user; the next
    // refresh cycle retries. The last good schedule for this calendar (if
    // any) stays armed.
    qCWarning(lcReminder) << "reminder fetch failed for" << calendarId << ":" << message;
}

void ReminderScheduler::rebuildTimers()
{
    qDeleteAll(m_pendingTimers);
    m_pendingTimers.clear();

    QList<Event> upcoming;
    QSet<QString> seen;
    for (const QString &calendarId : std::as_const(m_enabledCalendarIds)) {
        const auto it = m_upcomingByCalendar.constFind(calendarId);
        if (it == m_upcomingByCalendar.constEnd())
            continue;
        for (const Event &event : it.value()) {
            const QString dedupeKey = event.calendarId + QLatin1Char('\x1f') + event.id;
            if (seen.contains(dedupeKey))
                continue;
            seen.insert(dedupeKey);
            upcoming.append(event);
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime windowEnd = now.addSecs(qint64(kLookaheadHours) * 3600);
    const QList<PlannedReminder> planned = ReminderSchedule::plan(upcoming, now, windowEnd);

    int armed = 0;
    for (const PlannedReminder &entry : planned) {
        const QString key = ReminderSchedule::keyFor(entry.reminder.calendarId, entry.reminder.eventId,
                                                      entry.reminder.minutesBefore);
        if (m_firedKeys.contains(key))
            continue;
        ++armed;

        const qint64 msecs = qMax<qint64>(0, now.msecsTo(entry.fireAt));
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        const DueReminder reminder = entry.reminder;
        connect(timer, &QTimer::timeout, this, [this, key, reminder] {
            m_firedKeys.insert(key);
            emit reminderDue(reminder);
        });
        timer->start(int(qMin<qint64>(msecs, std::numeric_limits<int>::max())));
        m_pendingTimers.append(timer);
    }

    qCDebug(lcReminder) << "rebuild: upcoming events=" << upcoming.size()
                        << "planned popup reminders=" << planned.size()
                        << "armed timers=" << armed;
}
