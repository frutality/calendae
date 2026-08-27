#ifndef EVENTSCONTROLLER_H
#define EVENTSCONTROLLER_H

#include "calendar.h"
#include "event.h"

#include <QList>
#include <QObject>
#include <QString>
#include <optional>

// Common public contract shared by MonthEventsController and
// TimeGridEventsController, so MainWindow can populate/enable/clear/refresh
// all three view controllers (month, week, day) uniformly instead of
// repeating each call per concrete type. Each concrete controller still
// privately wires itself to its own view widget's navigation signal
// (displayedMonthChanged vs displayedRangeChanged) — only this outward-
// facing surface is unified.
class EventsController : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

public slots:
    // Full repopulate of the known calendar set; triggers a fresh fetch
    // cycle for every calendar currently marked selected.
    virtual void setCalendars(const QList<Calendar> &calendars) = 0;

    // Optimistic toggle: caller has already updated its own UI state.
    virtual void setCalendarEnabled(const QString &calendarId, bool enabled) = 0;

    // Sign-out: drop all state, blank the view.
    virtual void clear() = 0;

    // Forces a single calendar's cached events for the current range to be
    // discarded and (if enabled) re-fetched. No-op if no range is loaded or
    // calendarId is unknown.
    virtual void refreshCalendar(const QString &calendarId) = 0;

public:
    // Synchronous lookup into the current range's cached events. Returns
    // std::nullopt if not present in this controller's cache.
    virtual std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const = 0;

signals:
    void eventFetchFailed(const QString &message); // already staleness-filtered
};

#endif // EVENTSCONTROLLER_H
