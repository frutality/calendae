#include "storebackedeventscontroller.h"

#include "auth/authmanager.h"
#include "eventgridview.h"
#include "eventgrouping.h"
#include "montheventstore.h"

#include <utility>

StoreBackedEventsController::StoreBackedEventsController(AuthManager *authManager, MonthEventStore *store,
                                                        EventGridView *view, QObject *parent)
    : EventsController(parent)
    , m_authManager(authManager)
    , m_store(store)
    , m_view(view)
{
    connect(m_store, &MonthEventStore::bucketUpdated, this, &StoreBackedEventsController::onBucketUpdated);
    connect(m_store, &MonthEventStore::bucketFetchFailed, this, &StoreBackedEventsController::onBucketFetchFailed);
    connect(m_store, &MonthEventStore::bucketRefreshFailed, this, &StoreBackedEventsController::onBucketRefreshFailed);
}

bool StoreBackedEventsController::ready() const
{
    return m_authManager->state() == AuthManager::AuthState::SignedIn && !m_calendarsById.isEmpty();
}

void StoreBackedEventsController::setCalendars(const QList<Calendar> &calendars)
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
    // forever "could not load" for a now-404 calendar). reloadCurrentRange()
    // then clears the view and repaints only what's still enabled.
    m_enabledCalendarIds = std::move(selected);

    reloadCurrentRange();
}

void StoreBackedEventsController::setCalendarEnabled(const QString &calendarId, bool enabled)
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

void StoreBackedEventsController::clear()
{
    m_calendarsById.clear();
    m_enabledCalendarIds.clear();
    m_view->clearAllEvents();
}

void StoreBackedEventsController::refreshCalendar(const QString &calendarId)
{
    if (!m_calendarsById.contains(calendarId) || !ready())
        return;

    // The store bucket was just dropped by the caller; re-fetch it. The
    // currently-shown (now stale) events stay on screen until the reply
    // lands via onBucketUpdated() — no blank flash on save.
    m_store->ensureMonths(currentMonthKeys(), {calendarId});
}

void StoreBackedEventsController::refreshVisibleFromServer()
{
    if (!ready())
        return;

    // No clearAllEvents(), no immediate render, no maybeEmitCycleFinished():
    // the pill list stays put and onBucketUpdated() repaints per calendar as
    // the replies arrive (firing fetchCycleFinished() then, as usual).
    m_store->refreshVisible(currentMonthKeys(), m_enabledCalendarIds);
}

std::optional<Event> StoreBackedEventsController::findCachedEvent(const QString &calendarId, const QString &eventId) const
{
    for (const Event &event : m_store->eventsFor(calendarId, currentMonthKeys())) {
        if (event.id == eventId)
            return event;
    }
    return std::nullopt;
}

void StoreBackedEventsController::reloadCurrentRange()
{
    if (!ready())
        return;

    m_view->clearAllEvents();
    m_store->ensureMonths(currentMonthKeys(), m_enabledCalendarIds);
    for (const QString &calendarId : std::as_const(m_enabledCalendarIds))
        renderCalendarFromCache(calendarId);
    maybeEmitCycleFinished();
}

void StoreBackedEventsController::renderCalendarFromCache(const QString &calendarId)
{
    if (!m_enabledCalendarIds.contains(calendarId))
        return;
    const QList<Event> events = m_store->eventsFor(calendarId, currentMonthKeys());
    m_view->setEventsForCalendar(calendarId, EventGrouping::groupByDate(events, m_calendarsById));
}

void StoreBackedEventsController::maybeEmitCycleFinished()
{
    if (m_store->monthsSettled(currentMonthKeys(), m_enabledCalendarIds))
        emit fetchCycleFinished();
}

QString StoreBackedEventsController::calendarDisplayName(const QString &calendarId) const
{
    return m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
}

void StoreBackedEventsController::onBucketUpdated(const QDate &monthKey, const QString &calendarId)
{
    if (!currentMonthKeys().contains(monthKey))
        return; // navigated away since this fetch started
    renderCalendarFromCache(calendarId); // no-op if that calendar is disabled
    maybeEmitCycleFinished();
}

void StoreBackedEventsController::onBucketFetchFailed(const QDate &monthKey, const QString &calendarId,
                                                     const QString &message, bool transient)
{
    if (!currentMonthKeys().contains(monthKey))
        return;

    // A transient (offline) failure is reported once, globally, by the
    // window's connectivity indicator — don't also raise a per-calendar
    // "could not load" message for it.
    if (!transient)
        emit eventFetchFailed(tr("Could not load events for \"%1\": %2").arg(calendarDisplayName(calendarId), message));

    maybeEmitCycleFinished();
}

void StoreBackedEventsController::onBucketRefreshFailed(const QDate &monthKey, const QString &calendarId,
                                                       const QString &message, bool transient)
{
    if (!currentMonthKeys().contains(monthKey))
        return;

    // Previously-loaded events for this bucket are still on screen. A
    // transient failure is the window's global connectivity indicator's
    // job; a non-transient one (HTTP 500, or 403 after losing access to a
    // calendar) would otherwise be entirely silent. Either way the cycle
    // still has to be settled so fetchCycleFinished() fires.
    if (!transient)
        emit eventFetchFailed(tr("Could not refresh events for \"%1\": %2").arg(calendarDisplayName(calendarId), message));

    maybeEmitCycleFinished();
}
