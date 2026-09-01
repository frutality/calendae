#include "reminderscheduler.h"

#include "auth/authmanager.h"
#include "googlecalendarapi.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QTimer>

#include <limits>
#include <utility>

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
    QSet<QString> selected;
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (calendar.selected)
            selected.insert(calendar.id);
    }

    // Reconcile to the server's (authoritative) selection: a calendar
    // deselected or deleted elsewhere must stop firing reminders, not keep
    // popping notifications from its last-fetched schedule. Mirror the
    // cleanup setCalendarEnabled(false) does for each dropped calendar.
    const QSet<QString> dropped = m_enabledCalendarIds - selected;
    for (const QString &calendarId : dropped) {
        m_upcomingByCalendar.remove(calendarId);
        m_latestRequestIdByCalendar.remove(calendarId);
    }
    m_enabledCalendarIds = std::move(selected);
    if (!dropped.isEmpty())
        rebuildTimers(); // kill any timers already armed for the dropped calendars

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

    // Drop the now-stale cached events and immediately re-plan from what's
    // left: a one-shot timer already armed for an event that was just
    // deleted or rescheduled must not survive to fire before the re-fetch
    // reply lands (which may fail transiently and only log).
    m_upcomingByCalendar.remove(calendarId);
    rebuildTimers();
    if (m_enabledCalendarIds.contains(calendarId))
        fetchForCalendar(calendarId);
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
                                                      entry.reminder.minutesBefore, entry.fireAt);
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
