#include "timegrideventscontroller.h"

#include "auth/authmanager.h"
#include "eventgrouping.h"
#include "montheventstore.h"
#include "monthkeys.h"
#include "timegridviewwidget.h"

#include <utility>

TimeGridEventsController::TimeGridEventsController(AuthManager *authManager, MonthEventStore *store,
                                                     TimeGridViewWidget *view, QObject *parent)
    : EventsController(parent)
    , m_authManager(authManager)
    , m_store(store)
    , m_view(view)
{
    connect(m_view, &TimeGridViewWidget::displayedRangeChanged, this, &TimeGridEventsController::onDisplayedRangeChanged);
    connect(m_store, &MonthEventStore::bucketUpdated, this, &TimeGridEventsController::onBucketUpdated);
    connect(m_store, &MonthEventStore::bucketFetchFailed, this, &TimeGridEventsController::onBucketFetchFailed);
}

bool TimeGridEventsController::ready() const
{
    return m_authManager->state() == AuthManager::AuthState::SignedIn && !m_calendarsById.isEmpty();
}

QList<QDate> TimeGridEventsController::currentMonthKeys() const
{
    const QDate start = m_view->rangeStart();
    if (!start.isValid())
        return {};
    return MonthKeys::forDateRange(start, start.addDays(m_view->dayCount() - 1));
}

void TimeGridEventsController::setCalendars(const QList<Calendar> &calendars)
{
    m_calendarsById.clear();
    QSet<QString> selected;
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (calendar.selected)
            selected.insert(calendar.id);
    }

    // See MonthEventsController::setCalendars: reconcile to the server's
    // (authoritative) selection so a calendar deselected or deleted
    // elsewhere stops being fetched and rendered. reloadCurrentRange()
    // clears the view and repaints only what's still enabled.
    m_enabledCalendarIds = std::move(selected);

    reloadCurrentRange();
}

void TimeGridEventsController::setCalendarEnabled(const QString &calendarId, bool enabled)
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
        m_view->clearEventsForCalendar(calendarId);
    }
    maybeEmitCycleFinished();
}

void TimeGridEventsController::clear()
{
    m_calendarsById.clear();
    m_enabledCalendarIds.clear();
    m_view->clearAllEvents();
}

void TimeGridEventsController::refreshCalendar(const QString &calendarId)
{
    if (!m_calendarsById.contains(calendarId) || !ready())
        return;

    // The store bucket was just dropped by the caller; re-fetch it. The
    // currently-shown (now stale) events stay on screen until the reply
    // lands via onBucketUpdated() — no blank flash on save.
    m_store->ensureMonths(currentMonthKeys(), {calendarId});
}

void TimeGridEventsController::refreshVisibleFromServer()
{
    if (!ready())
        return;

    // See MonthEventsController::refreshVisibleFromServer(): silent re-fetch
    // of the visible range, no view clear, repaint driven by onBucketUpdated().
    m_store->refreshVisible(currentMonthKeys(), m_enabledCalendarIds);
}

std::optional<Event> TimeGridEventsController::findCachedEvent(const QString &calendarId, const QString &eventId) const
{
    for (const Event &event : m_store->eventsFor(calendarId, currentMonthKeys())) {
        if (event.id == eventId)
            return event;
    }
    return std::nullopt;
}

void TimeGridEventsController::onDisplayedRangeChanged(const QDate &rangeStart)
{
    Q_UNUSED(rangeStart);
    reloadCurrentRange();
}

void TimeGridEventsController::reloadCurrentRange()
{
    if (!ready())
        return;

    m_view->clearAllEvents();
    m_store->ensureMonths(currentMonthKeys(), m_enabledCalendarIds);
    for (const QString &calendarId : std::as_const(m_enabledCalendarIds))
        renderCalendarFromCache(calendarId);
    maybeEmitCycleFinished();
}

void TimeGridEventsController::renderCalendarFromCache(const QString &calendarId)
{
    if (!m_enabledCalendarIds.contains(calendarId))
        return;
    const QList<Event> events = m_store->eventsFor(calendarId, currentMonthKeys());
    m_view->setEventsForCalendar(calendarId, EventGrouping::groupByDate(events, m_calendarsById));
}

void TimeGridEventsController::maybeEmitCycleFinished()
{
    if (m_store->monthsSettled(currentMonthKeys(), m_enabledCalendarIds))
        emit fetchCycleFinished();
}

void TimeGridEventsController::onBucketUpdated(const QDate &monthKey, const QString &calendarId)
{
    if (!currentMonthKeys().contains(monthKey))
        return; // navigated away since this fetch started
    renderCalendarFromCache(calendarId); // no-op if that calendar is disabled
    maybeEmitCycleFinished();
}

void TimeGridEventsController::onBucketFetchFailed(const QDate &monthKey, const QString &calendarId,
                                                    const QString &message, bool transient)
{
    if (!currentMonthKeys().contains(monthKey))
        return;

    // See MonthEventsController::onBucketFetchFailed: transient failures are
    // surfaced only by the window-level connectivity indicator.
    if (!transient) {
        const QString calendarName = m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
        emit eventFetchFailed(tr("Could not load events for \"%1\": %2").arg(calendarName, message));
    }

    maybeEmitCycleFinished();
}
