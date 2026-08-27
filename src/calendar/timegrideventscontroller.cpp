#include "timegrideventscontroller.h"

#include "auth/authmanager.h"
#include "eventgrouping.h"
#include "eventtimerange.h"
#include "googlecalendarapi.h"
#include "timegridrange.h"
#include "timegridviewwidget.h"

#include <utility>

TimeGridEventsController::TimeGridEventsController(AuthManager *authManager, GoogleCalendarApi *calendarApi,
                                                     TimeGridViewWidget *view, QObject *parent)
    : EventsController(parent)
    , m_authManager(authManager)
    , m_calendarApi(calendarApi)
    , m_view(view)
{
    connect(m_view, &TimeGridViewWidget::displayedRangeChanged, this, &TimeGridEventsController::onDisplayedRangeChanged);
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetched, this, &TimeGridEventsController::onEventsFetched);
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetchFailed, this, &TimeGridEventsController::onEventsFetchFailed);
}

void TimeGridEventsController::setCalendars(const QList<Calendar> &calendars)
{
    m_calendarsById.clear();
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (!m_enabledCalendarIds.contains(calendar.id) && calendar.selected)
            m_enabledCalendarIds.insert(calendar.id);
    }

    startFetchCycleForCurrentRange();
}

void TimeGridEventsController::setCalendarEnabled(const QString &calendarId, bool enabled)
{
    if (!m_calendarsById.contains(calendarId))
        return;

    if (enabled) {
        m_enabledCalendarIds.insert(calendarId);
        if (m_cachedEventsByCalendar.contains(calendarId))
            m_view->setEventsForCalendar(calendarId, EventGrouping::groupByDate(m_cachedEventsByCalendar.value(calendarId), m_calendarsById));
        else if (m_cachedRangeStart.isValid())
            fetchForCalendar(calendarId);
    } else {
        m_enabledCalendarIds.remove(calendarId);
        m_view->clearEventsForCalendar(calendarId);
    }
}

void TimeGridEventsController::clear()
{
    m_calendarsById.clear();
    m_enabledCalendarIds.clear();
    m_cachedRangeStart = QDate();
    m_cachedEventsByCalendar.clear();
    m_activeRequestIds.clear();
    m_view->clearAllEvents();
}

void TimeGridEventsController::refreshCalendar(const QString &calendarId)
{
    if (!m_calendarsById.contains(calendarId))
        return;
    if (!m_cachedRangeStart.isValid())
        return; // no range loaded yet (e.g. not signed in)

    m_cachedEventsByCalendar.remove(calendarId);
    if (m_enabledCalendarIds.contains(calendarId))
        fetchForCalendar(calendarId); // new requestId, tracked in m_activeRequestIds like any other fetch
}

std::optional<Event> TimeGridEventsController::findCachedEvent(const QString &calendarId, const QString &eventId) const
{
    const auto it = m_cachedEventsByCalendar.constFind(calendarId);
    if (it == m_cachedEventsByCalendar.constEnd())
        return std::nullopt;
    for (const Event &event : it.value()) {
        if (event.id == eventId)
            return event;
    }
    return std::nullopt;
}

void TimeGridEventsController::onDisplayedRangeChanged(const QDate &rangeStart)
{
    Q_UNUSED(rangeStart);
    startFetchCycleForCurrentRange();
}

void TimeGridEventsController::startFetchCycleForCurrentRange()
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn)
        return;
    if (m_calendarsById.isEmpty())
        return;

    m_cachedRangeStart = m_view->rangeStart();
    m_cachedEventsByCalendar.clear();
    m_activeRequestIds.clear();
    m_view->clearAllEvents();

    for (const QString &calendarId : std::as_const(m_enabledCalendarIds))
        fetchForCalendar(calendarId);
}

void TimeGridEventsController::fetchForCalendar(const QString &calendarId)
{
    const QList<QDate> dates = TimeGridRange::datesForRange(m_cachedRangeStart, m_view->dayCount());
    const EventTimeRange::Range range = EventTimeRange::forDates(dates);

    const quint64 requestId = m_calendarApi->fetchEvents(calendarId, range.timeMin, range.timeMax);
    m_activeRequestIds.insert(requestId);
}

void TimeGridEventsController::onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events)
{
    if (!m_activeRequestIds.remove(requestId))
        return; // stale reply — discard

    m_cachedEventsByCalendar.insert(calendarId, events);
    if (m_enabledCalendarIds.contains(calendarId))
        m_view->setEventsForCalendar(calendarId, EventGrouping::groupByDate(events, m_calendarsById));
}

void TimeGridEventsController::onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message)
{
    if (!m_activeRequestIds.remove(requestId))
        return; // stale reply — discard

    const QString calendarName = m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
    emit eventFetchFailed(tr("Could not load events for \"%1\": %2").arg(calendarName, message));
}
