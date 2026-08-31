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

    // Brings a single calendar's events for the current range back in sync
    // after an out-of-band mutation (event create / update / delete). The
    // view controllers share one MonthEventStore, so the caller is expected
    // to drop that calendar's cached buckets (MonthEventStore::
    // invalidateCalendar) once before fanning this out to every controller;
    // ReminderScheduler manages its own cache and needs no such call. No-op
    // if calendarId is unknown or nothing is loaded yet.
    virtual void refreshCalendar(const QString &calendarId) = 0;

    // MainWindow's ~10-minute safety poll calls this on whichever view
    // controller is currently on screen. Unlike refreshCalendar(), the store
    // has NOT been invalidated first — this re-hits the network for the
    // on-screen buckets past their freshness TTL, to catch edits made in the
    // Google web UI while the window sat idle. No view clear: current events
    // stay put until the reply lands. Default no-op — ReminderScheduler runs
    // its own rolling fetch and ignores this.
    virtual void refreshVisibleFromServer() {}

public:
    // Synchronous lookup into the current range's cached events. Returns
    // std::nullopt if not present in this controller's cache.
    virtual std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const = 0;

signals:
    void eventFetchFailed(const QString &message); // already staleness-filtered

    // Emitted once the current fetch cycle has settled — every request that
    // was started has completed (success or failure), or the cache already
    // held everything and nothing was requested (in which case this fires
    // synchronously). I.e. the view's content is now at its final,
    // real-data state. Used by MainWindow to know when it's
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
