#ifndef MONTHEVENTSCONTROLLER_H
#define MONTHEVENTSCONTROLLER_H

#include "calendar.h"
#include "event.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

class AuthManager;
class GoogleCalendarApi;
class MonthViewWidget;
struct MonthDayEventItem;

// Owns which calendars are enabled and coordinates fetching their events
// for the month view's currently displayed 42-day grid: fans out one
// events.list request per enabled calendar, discards stale replies (a
// slow reply arriving after month navigation, sign-out, or the calendar
// being deselected before it landed), and caches one month's worth of
// per-calendar results so toggling a calendar's visibility doesn't
// require a network round-trip.
class MonthEventsController : public QObject
{
    Q_OBJECT
public:
    explicit MonthEventsController(AuthManager *authManager, GoogleCalendarApi *calendarApi,
                                    MonthViewWidget *monthView, QObject *parent = nullptr);

public slots:
    // Full repopulate of the known calendar set (from GoogleCalendarApi's
    // calendarListFetched); triggers a fresh fetch cycle for every
    // calendar currently marked selected.
    void setCalendars(const QList<Calendar> &calendars);

    // Optimistic toggle: caller has already updated its own UI state.
    void setCalendarEnabled(const QString &calendarId, bool enabled);

    // Sign-out: drop all state, blank the month view.
    void clear();

    // Forces a single calendar's cached events for the current month to be
    // discarded and (if that calendar is currently enabled) re-fetched. For
    // use after an out-of-band mutation (e.g. event creation) that the
    // normal enable/disable/navigate flows don't cover. If the calendar is
    // currently disabled, only the cache is dropped — disabled calendars'
    // events are never shown, and a stale cache entry self-corrects on next
    // enable. No-op if no month is currently loaded or calendarId is unknown.
    void refreshCalendar(const QString &calendarId);

public:
    // Synchronous lookup into the current month's cached events (same data
    // backing the visible grid) — used to prefill an edit dialog without a
    // network round-trip. Returns std::nullopt if calendarId/eventId isn't
    // in the cache (e.g. a stale pill click racing a calendar being
    // disabled or the month changing) — callers must treat this
    // defensively.
    std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const;

signals:
    void eventFetchFailed(const QString &message); // already staleness-filtered

private slots:
    void onDisplayedMonthChanged(const QDate &firstOfMonth);
    void onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events);
    void onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message);

private:
    void startFetchCycleForCurrentMonth();
    void fetchForCalendar(const QString &calendarId);
    QHash<QDate, QList<MonthDayEventItem>> groupByDate(const QList<Event> &events) const;

    AuthManager *m_authManager;
    GoogleCalendarApi *m_calendarApi;
    MonthViewWidget *m_monthView;

    QHash<QString, Calendar> m_calendarsById;
    QSet<QString> m_enabledCalendarIds;

    QDate m_cachedMonthKey; // invalid initially
    // Cached events, with display fields already localized (.toLocalTime())
    // at grouping time, for m_cachedMonthKey ONLY. Deliberately NOT
    // recomputed on a live system-timezone change mid-session (e.g. a
    // laptop traveling while the app stays open on the same month) — Qt has
    // no portable cross-platform "timezone changed" signal, and any
    // navigation/refresh self-corrects it immediately, so watching for this
    // rare case was judged not worth the added complexity.
    QHash<QString, QList<Event>> m_cachedEventsByCalendar;

    quint64 m_nextRequestId = 1;
    QSet<quint64> m_activeRequestIds;
};

#endif // MONTHEVENTSCONTROLLER_H
