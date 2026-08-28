#ifndef EVENTSCONTROLLER_H
#define EVENTSCONTROLLER_H

#include "calendar.h"
#include "event.h"

#include <QList>
#include <QObject>
#include <QString>
#include <optional>

// Common public contract shared by MonthEventsController,
// TimeGridEventsController, and the non-view ReminderScheduler, so
// MainWindow can populate/enable/clear/refresh every controller uniformly
// (in one loop over m_eventsControllers) instead of repeating each call per
// concrete type. The view controllers privately wire themselves to their
// own view widget's navigation signal (displayedMonthChanged vs
// displayedRangeChanged); ReminderScheduler instead runs its own rolling
// fetch on a timer. Only this outward-facing surface is unified.
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

    // Emitted once every in-flight request of the current fetch cycle has
    // completed (success or failure) — i.e. the view's content has settled
    // to its final, real-data state. Used by MainWindow to know when it's
    // safe to (re-)apply a saved scroll position: right after startup, a
    // view's scrollable content can still be its empty/placeholder size
    // (month view's day cells only grow once real events load — unlike
    // week/day's fixed-height time grid, though even there the all-day
    // strip growing can shrink the scroll viewport slightly), so applying a
    // saved scroll position immediately can get silently clamped to
    // whatever range existed at that moment.
    void fetchCycleFinished();
};

#endif // EVENTSCONTROLLER_H
