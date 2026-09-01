#ifndef MONTHEVENTSCONTROLLER_H
#define MONTHEVENTSCONTROLLER_H

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
class MonthEventStore;
class MonthViewWidget;

// Owns which calendars are enabled for the month view and turns the shared
// MonthEventStore's cached events into the 42-day grid's per-calendar
// display lists. Holds no events or fetch bookkeeping of its own — the
// store fans out the events.list requests, deduplicates late replies, and
// caches by (month, calendar); this class just asks it to keep the visible
// month loaded and re-renders whenever a bucket lands.
class MonthEventsController : public EventsController
{
    Q_OBJECT
public:
    explicit MonthEventsController(AuthManager *authManager, MonthEventStore *store,
                                    MonthViewWidget *monthView, QObject *parent = nullptr);

public slots:
    void setCalendars(const QList<Calendar> &calendars) override;
    void setCalendarEnabled(const QString &calendarId, bool enabled) override;
    void clear() override;

    // The store bucket for calendarId is expected to have already been
    // dropped (MonthEventStore::invalidateCalendar) by the caller before
    // this fan-out; this just re-ensures and re-renders the visible month.
    void refreshCalendar(const QString &calendarId) override;

    void refreshVisibleFromServer() override;

public:
    std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const override;

private slots:
    void onDisplayedMonthChanged(const QDate &firstOfMonth);
    void onBucketUpdated(const QDate &monthKey, const QString &calendarId);
    void onBucketFetchFailed(const QDate &monthKey, const QString &calendarId, const QString &message, bool transient);
    void onBucketRefreshFailed(const QDate &monthKey, const QString &calendarId, const QString &message, bool transient);

private:
    bool ready() const;
    // Just the displayed month, NOT every month the 42-day grid touches:
    // the store fetches each month bucket over that month's full 42-day
    // grid range, so one bucket already covers the grid's leading/trailing
    // adjacent-month days. Spanning it across 3 months here would triple the
    // cold month-view load.
    QList<QDate> currentMonthKeys() const;
    void reloadCurrentMonth();
    void renderCalendarFromCache(const QString &calendarId);
    void maybeEmitCycleFinished();

    AuthManager *m_authManager;
    MonthEventStore *m_store;
    MonthViewWidget *m_monthView;

    QHash<QString, Calendar> m_calendarsById;
    QSet<QString> m_enabledCalendarIds;
};

#endif // MONTHEVENTSCONTROLLER_H
