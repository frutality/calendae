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
class MonthEventStore;
class TimeGridViewWidget;

// Same role as MonthEventsController, for the week/day time-grid view: keeps
// the shared MonthEventStore loaded for whichever calendar month(s) the
// view's dayCount-day range touches (one month usually, two when the range
// straddles a month boundary) and renders from the store's cache. The store
// owns all fetching and staleness handling; a week or day inside an
// already-loaded month costs no network round-trip. One instance per view
// (week and day navigate independently).
class TimeGridEventsController : public EventsController
{
    Q_OBJECT
public:
    explicit TimeGridEventsController(AuthManager *authManager, MonthEventStore *store,
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
    void onBucketUpdated(const QDate &monthKey, const QString &calendarId);
    void onBucketFetchFailed(const QDate &monthKey, const QString &calendarId, const QString &message);

private:
    bool ready() const;
    QList<QDate> currentMonthKeys() const; // month(s) the visible range touches
    void reloadCurrentRange();
    void renderCalendarFromCache(const QString &calendarId);
    void maybeEmitCycleFinished();

    AuthManager *m_authManager;
    MonthEventStore *m_store;
    TimeGridViewWidget *m_view;

    QHash<QString, Calendar> m_calendarsById;
    QSet<QString> m_enabledCalendarIds;
};

#endif // TIMEGRIDEVENTSCONTROLLER_H
