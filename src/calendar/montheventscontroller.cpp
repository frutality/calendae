#include "montheventscontroller.h"

#include "auth/authmanager.h"
#include "eventgrouping.h"
#include "montheventitem.h"
#include "montheventstore.h"
#include "monthkeys.h"
#include "monthviewwidget.h"

#include <utility>

MonthEventsController::MonthEventsController(AuthManager *authManager, MonthEventStore *store,
                                               MonthViewWidget *monthView, QObject *parent)
    : EventsController(parent)
    , m_authManager(authManager)
    , m_store(store)
    , m_monthView(monthView)
{
    connect(m_monthView, &MonthViewWidget::displayedMonthChanged, this, &MonthEventsController::onDisplayedMonthChanged);
    connect(m_store, &MonthEventStore::bucketUpdated, this, &MonthEventsController::onBucketUpdated);
    connect(m_store, &MonthEventStore::bucketFetchFailed, this, &MonthEventsController::onBucketFetchFailed);
}

bool MonthEventsController::ready() const
{
    return m_authManager->state() == AuthManager::AuthState::SignedIn && !m_calendarsById.isEmpty();
}

QList<QDate> MonthEventsController::currentMonthKeys() const
{
    const QDate month = MonthKeys::normalize(m_monthView->displayedMonth());
    return month.isValid() ? QList<QDate>{month} : QList<QDate>{};
}

void MonthEventsController::setCalendars(const QList<Calendar> &calendars)
{
    m_calendarsById.clear();
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (!m_enabledCalendarIds.contains(calendar.id) && calendar.selected)
            m_enabledCalendarIds.insert(calendar.id);
    }

    reloadCurrentMonth();
}

void MonthEventsController::setCalendarEnabled(const QString &calendarId, bool enabled)
{
    if (!m_calendarsById.contains(calendarId))
        return;

    if (enabled) {
        m_enabledCalendarIds.insert(calendarId);
        if (ready()) {
            m_store->ensureMonths(currentMonthKeys(), {calendarId});
            renderCalendarFromCache(calendarId);
        }
    } else {
        m_enabledCalendarIds.remove(calendarId);
        m_monthView->clearEventsForCalendar(calendarId);
    }
    maybeEmitCycleFinished();
}

void MonthEventsController::clear()
{
    m_calendarsById.clear();
    m_enabledCalendarIds.clear();
    m_monthView->clearAllEvents();
}

void MonthEventsController::refreshCalendar(const QString &calendarId)
{
    if (!m_calendarsById.contains(calendarId) || !ready())
        return;

    // The store bucket was just dropped by the caller; re-fetch it. The
    // currently-shown (now stale) events stay on screen until the reply
    // lands via onBucketUpdated() — no blank flash on save.
    m_store->ensureMonths(currentMonthKeys(), {calendarId});
}

std::optional<Event> MonthEventsController::findCachedEvent(const QString &calendarId, const QString &eventId) const
{
    for (const Event &event : m_store->eventsFor(calendarId, currentMonthKeys())) {
        if (event.id == eventId)
            return event;
    }
    return std::nullopt;
}

void MonthEventsController::onDisplayedMonthChanged(const QDate &firstOfMonth)
{
    Q_UNUSED(firstOfMonth);
    reloadCurrentMonth();
}

void MonthEventsController::reloadCurrentMonth()
{
    if (!ready())
        return;

    m_monthView->clearAllEvents();
    m_store->ensureMonths(currentMonthKeys(), m_enabledCalendarIds);
    for (const QString &calendarId : std::as_const(m_enabledCalendarIds))
        renderCalendarFromCache(calendarId);
    maybeEmitCycleFinished();
}

void MonthEventsController::renderCalendarFromCache(const QString &calendarId)
{
    if (!m_enabledCalendarIds.contains(calendarId))
        return;
    const QList<Event> events = m_store->eventsFor(calendarId, currentMonthKeys());
    m_monthView->setEventsForCalendar(calendarId, EventGrouping::groupByDate(events, m_calendarsById));
}

void MonthEventsController::maybeEmitCycleFinished()
{
    if (m_store->monthsSettled(currentMonthKeys(), m_enabledCalendarIds))
        emit fetchCycleFinished();
}

void MonthEventsController::onBucketUpdated(const QDate &monthKey, const QString &calendarId)
{
    if (!currentMonthKeys().contains(monthKey))
        return; // navigated away since this fetch started
    renderCalendarFromCache(calendarId); // no-op if that calendar is disabled
    maybeEmitCycleFinished();
}

void MonthEventsController::onBucketFetchFailed(const QDate &monthKey, const QString &calendarId,
                                                 const QString &message, bool transient)
{
    if (!currentMonthKeys().contains(monthKey))
        return;

    // A transient (offline) failure is reported once, globally, by the
    // window's connectivity indicator — don't also raise a per-calendar
    // "could not load" message for it.
    if (!transient) {
        const QString calendarName = m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
        emit eventFetchFailed(tr("Could not load events for \"%1\": %2").arg(calendarName, message));
    }

    maybeEmitCycleFinished();
}
