#include "montheventscontroller.h"

#include "auth/authmanager.h"
#include "eventgrouping.h"
#include "eventtimerange.h"
#include "googlecalendarapi.h"
#include "monthgrid.h"
#include "montheventitem.h"
#include "monthviewwidget.h"

#include <QLocale>
#include <utility>

MonthEventsController::MonthEventsController(AuthManager *authManager, GoogleCalendarApi *calendarApi,
                                               MonthViewWidget *monthView, QObject *parent)
    : EventsController(parent)
    , m_authManager(authManager)
    , m_calendarApi(calendarApi)
    , m_monthView(monthView)
{
    connect(m_monthView, &MonthViewWidget::displayedMonthChanged, this, &MonthEventsController::onDisplayedMonthChanged);
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetched, this, &MonthEventsController::onEventsFetched);
    connect(m_calendarApi, &GoogleCalendarApi::eventsFetchFailed, this, &MonthEventsController::onEventsFetchFailed);
}

void MonthEventsController::setCalendars(const QList<Calendar> &calendars)
{
    m_calendarsById.clear();
    for (const Calendar &calendar : calendars) {
        m_calendarsById.insert(calendar.id, calendar);
        if (!m_enabledCalendarIds.contains(calendar.id) && calendar.selected)
            m_enabledCalendarIds.insert(calendar.id);
    }

    startFetchCycleForCurrentMonth();
}

void MonthEventsController::setCalendarEnabled(const QString &calendarId, bool enabled)
{
    if (!m_calendarsById.contains(calendarId))
        return;

    if (enabled) {
        m_enabledCalendarIds.insert(calendarId);
        if (m_cachedEventsByCalendar.contains(calendarId))
            m_monthView->setEventsForCalendar(calendarId, EventGrouping::groupByDate(m_cachedEventsByCalendar.value(calendarId), m_calendarsById));
        else if (m_cachedMonthKey.isValid())
            fetchForCalendar(calendarId);
    } else {
        m_enabledCalendarIds.remove(calendarId);
        m_monthView->clearEventsForCalendar(calendarId);
    }
}

void MonthEventsController::clear()
{
    m_calendarsById.clear();
    m_enabledCalendarIds.clear();
    m_cachedMonthKey = QDate();
    m_cachedEventsByCalendar.clear();
    m_activeRequestIds.clear();
    m_monthView->clearAllEvents();
}

void MonthEventsController::refreshCalendar(const QString &calendarId)
{
    if (!m_calendarsById.contains(calendarId))
        return;
    if (!m_cachedMonthKey.isValid())
        return; // no month loaded yet (e.g. not signed in)

    m_cachedEventsByCalendar.remove(calendarId);
    if (m_enabledCalendarIds.contains(calendarId))
        fetchForCalendar(calendarId); // new requestId, tracked in m_activeRequestIds like any other fetch
}

std::optional<Event> MonthEventsController::findCachedEvent(const QString &calendarId, const QString &eventId) const
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

void MonthEventsController::onDisplayedMonthChanged(const QDate &firstOfMonth)
{
    Q_UNUSED(firstOfMonth);
    startFetchCycleForCurrentMonth();
}

void MonthEventsController::startFetchCycleForCurrentMonth()
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn)
        return;
    if (m_calendarsById.isEmpty())
        return;

    m_cachedMonthKey = m_monthView->displayedMonth();
    m_cachedEventsByCalendar.clear();
    m_activeRequestIds.clear();
    m_monthView->clearAllEvents();

    for (const QString &calendarId : std::as_const(m_enabledCalendarIds))
        fetchForCalendar(calendarId);
}

void MonthEventsController::fetchForCalendar(const QString &calendarId)
{
    const QList<QDate> dates = MonthGrid::datesForGrid(m_cachedMonthKey, QLocale::system().firstDayOfWeek());
    const EventTimeRange::Range range = EventTimeRange::forDates(dates);

    const quint64 requestId = m_calendarApi->fetchEvents(calendarId, range.timeMin, range.timeMax);
    m_activeRequestIds.insert(requestId);
}

void MonthEventsController::onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events)
{
    if (!m_activeRequestIds.remove(requestId))
        return; // stale reply — discard

    m_cachedEventsByCalendar.insert(calendarId, events);
    if (m_enabledCalendarIds.contains(calendarId))
        m_monthView->setEventsForCalendar(calendarId, EventGrouping::groupByDate(events, m_calendarsById));

    if (m_activeRequestIds.isEmpty())
        emit fetchCycleFinished();
}

void MonthEventsController::onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message)
{
    if (!m_activeRequestIds.remove(requestId))
        return; // stale reply — discard

    const QString calendarName = m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
    emit eventFetchFailed(tr("Could not load events for \"%1\": %2").arg(calendarName, message));

    if (m_activeRequestIds.isEmpty())
        emit fetchCycleFinished();
}
