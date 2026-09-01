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
    connect(m_store, &MonthEventStore::bucketRefreshFailed, this, &MonthEventsController::onBucketRefreshFailed);
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
    QSet<QString> selected;
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (calendar.selected)
            selected.insert(calendar.id);
    }

    // The server's `selected` flag is authoritative: local sidebar toggles
    // are PATCHed to Google and come back here, so a calendar deselected or
    // deleted elsewhere must drop out of the fetch/render set rather than
    // linger (its events stuck on screen under an unchecked box, or a
    // forever "could not load" for a now-404 calendar). reloadCurrentMonth()
    // then clears the view and repaints only what's still enabled.
    m_enabledCalendarIds = std::move(selected);

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

void MonthEventsController::refreshVisibleFromServer()
{
    if (!ready())
        return;

    // No clearAllEvents(), no immediate render, no maybeEmitCycleFinished():
    // the pill list stays put and onBucketUpdated() repaints per calendar as
    // the replies arrive (firing fetchCycleFinished() then, as usual).
    m_store->refreshVisible(currentMonthKeys(), m_enabledCalendarIds);
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

void MonthEventsController::onBucketRefreshFailed(const QDate &monthKey, const QString &calendarId,
                                                  const QString &message, bool transient)
{
    if (!currentMonthKeys().contains(monthKey))
        return;

    // Previously-loaded events for this bucket are still on screen. A
    // transient failure is the window's global connectivity indicator's
    // job; a non-transient one (HTTP 500, or 403 after losing access to a
    // calendar) would otherwise be entirely silent. Either way the cycle
    // still has to be settled so fetchCycleFinished() fires.
    if (!transient) {
        const QString calendarName = m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
        emit eventFetchFailed(tr("Could not refresh events for \"%1\": %2").arg(calendarName, message));
    }

    maybeEmitCycleFinished();
}
