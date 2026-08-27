#ifndef TIMEGRIDEVENTSCONTROLLER_H
#define TIMEGRIDEVENTSCONTROLLER_H

#include "calendar.h"
#include "event.h"
#include "eventscontroller.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <optional>

class AuthManager;
class GoogleCalendarApi;
class TimeGridViewWidget;

// Same role as MonthEventsController, but for the week/day time-grid view:
// coordinates fetching enabled calendars' events for the view's currently
// displayed dayCount-day range (7 for week, 1 for day) instead of a 42-day
// month grid. One instance per view (week and day each get their own,
// since each view's navigation/cache is independent).
class TimeGridEventsController : public EventsController
{
    Q_OBJECT
public:
    explicit TimeGridEventsController(AuthManager *authManager, GoogleCalendarApi *calendarApi,
                                       TimeGridViewWidget *view, QObject *parent = nullptr);

public slots:
    void setCalendars(const QList<Calendar> &calendars) override;
    void setCalendarEnabled(const QString &calendarId, bool enabled) override;
    void clear() override;
    void refreshCalendar(const QString &calendarId) override;

public:
    std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const override;

private slots:
    void onDisplayedRangeChanged(const QDate &rangeStart);
    void onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events);
    void onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message);

private:
    void startFetchCycleForCurrentRange();
    void fetchForCalendar(const QString &calendarId);

    AuthManager *m_authManager;
    GoogleCalendarApi *m_calendarApi;
    TimeGridViewWidget *m_view;

    QHash<QString, Calendar> m_calendarsById;
    QSet<QString> m_enabledCalendarIds;

    QDate m_cachedRangeStart; // invalid initially
    QHash<QString, QList<Event>> m_cachedEventsByCalendar; // for m_cachedRangeStart ONLY

    QSet<quint64> m_activeRequestIds;
};

#endif // TIMEGRIDEVENTSCONTROLLER_H
