#include "montheventscontroller.h"

#include "auth/authmanager.h"
#include "eventtimerange.h"
#include "googlecalendarapi.h"
#include "monthgrid.h"
#include "montheventitem.h"
#include "monthviewwidget.h"

#include <QLocale>
#include <utility>

MonthEventsController::MonthEventsController(AuthManager *authManager, GoogleCalendarApi *calendarApi,
                                               MonthViewWidget *monthView, QObject *parent)
    : QObject(parent)
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
            m_monthView->setEventsForCalendar(calendarId, groupByDate(m_cachedEventsByCalendar.value(calendarId)));
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

    const quint64 requestId = m_nextRequestId++;
    m_activeRequestIds.insert(requestId);
    m_calendarApi->fetchEvents(requestId, calendarId, range.timeMin, range.timeMax);
}

void MonthEventsController::onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events)
{
    if (!m_activeRequestIds.remove(requestId))
        return; // stale reply — discard

    m_cachedEventsByCalendar.insert(calendarId, events);
    if (m_enabledCalendarIds.contains(calendarId))
        m_monthView->setEventsForCalendar(calendarId, groupByDate(events));
}

void MonthEventsController::onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message)
{
    if (!m_activeRequestIds.remove(requestId))
        return; // stale reply — discard

    const QString calendarName = m_calendarsById.contains(calendarId) ? m_calendarsById.value(calendarId).summary : calendarId;
    emit eventFetchFailed(tr("Could not load events for \"%1\": %2").arg(calendarName, message));
}

QHash<QDate, QList<MonthDayEventItem>> MonthEventsController::groupByDate(const QList<Event> &events) const
{
    QHash<QDate, QList<MonthDayEventItem>> result;
    for (const Event &event : events) {
        MonthDayEventItem item;
        item.eventId = event.id;
        item.calendarId = event.calendarId;
        item.title = event.summary.isEmpty() ? tr("(No title)") : event.summary;
        item.color = m_calendarsById.contains(event.calendarId) ? m_calendarsById.value(event.calendarId).color : QColor(Qt::gray);
        item.allDay = event.allDay;
        if (!event.allDay) {
            item.timeLabel = QLocale::system().toString(event.startDateTime.toLocalTime().time(), QLocale::ShortFormat);
            item.startInstant = event.startDateTime;
        }

        // event.endDate is exclusive for all-day events (Google semantics:
        // a 1-day all-day event has endDate == startDate + 1), but inclusive
        // for timed events (it's simply which local day the end instant
        // falls on). Normalize to the last *inclusive* day, then repeat the
        // item on every day the event spans so multi-day events stay
        // visible on each covered day, not just their start date. Capped
        // defensively against malformed/pathological data.
        QDate lastDateInclusive = event.allDay ? event.endDate.addDays(-1) : event.endDate;
        if (lastDateInclusive < event.startDate)
            lastDateInclusive = event.startDate;

        constexpr qint64 kMaxSpanDays = 90;
        const qint64 spanDays = qBound<qint64>(0, event.startDate.daysTo(lastDateInclusive), kMaxSpanDays - 1);

        for (qint64 offset = 0; offset <= spanDays; ++offset)
            result[event.startDate.addDays(offset)].append(item);
    }
    return result;
}
