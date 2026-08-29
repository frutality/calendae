#ifndef REMINDERSCHEDULER_H
#define REMINDERSCHEDULER_H

#include "calendar.h"
#include "event.h"
#include "eventscontroller.h"
#include "reminderschedule.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <optional>

class AuthManager;
class GoogleCalendarApi;
QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

// A non-view EventsController: instead of feeding a month/week/day widget,
// it keeps its own rolling fetch of the next ~36h of events (independent of
// whatever range the visible views show) and arms a one-shot timer for each
// event that carries its own "popup" reminder override, emitting
// reminderDue() when one comes due. Calendar-default reminders
// (reminders.useDefault) are deliberately ignored — see ReminderSchedule.
//
// It plugs into MainWindow's m_eventsControllers list, so sign-out (clear),
// sidebar toggles (setCalendarEnabled) and post-mutation refreshes
// (refreshCalendar) all reach it for free; only the initial setCalendars()
// is driven explicitly (it must always populate, unlike the lazily
// populated view controllers).
class ReminderScheduler : public EventsController
{
    Q_OBJECT
public:
    explicit ReminderScheduler(AuthManager *authManager, GoogleCalendarApi *calendarApi, QObject *parent = nullptr);

public slots:
    void setCalendars(const QList<Calendar> &calendars) override;
    void setCalendarEnabled(const QString &calendarId, bool enabled) override;
    void clear() override;
    void refreshCalendar(const QString &calendarId) override;

    // Re-fetch the whole look-ahead window for every enabled calendar, so a
    // change made elsewhere (a new event added in the Google web UI) is
    // reflected without waiting for the 10-minute periodic refresh. Wired to
    // the views' fetchCycleFinished so it piggybacks on any navigation /
    // view switch; throttled so rapid navigation doesn't fan out repeated
    // fetches.
    void refreshAll();

public:
    std::optional<Event> findCachedEvent(const QString &calendarId, const QString &eventId) const override;

signals:
    void reminderDue(const DueReminder &reminder);

private slots:
    void onEventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events);
    void onEventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message, bool transient);

private:
    void startFetchCycle();
    void fetchForCalendar(const QString &calendarId);
    void rebuildTimers();

    AuthManager *m_authManager;
    GoogleCalendarApi *m_calendarApi;
    QTimer *m_refreshTimer;

    QHash<QString, Calendar> m_calendarsById;
    QSet<QString> m_enabledCalendarIds;

    // Next ~36h of events per enabled calendar; overwritten per calendar as
    // each fetch reply lands (not cleared at cycle start), so a single
    // failed fetch doesn't blank an otherwise-valid schedule.
    QHash<QString, QList<Event>> m_upcomingByCalendar;

    // Newest in-flight fetch per calendar; a reply whose id doesn't match is
    // a superseded/stale fetch and is dropped.
    QHash<QString, quint64> m_latestRequestIdByCalendar;

    // Reminder keys (ReminderSchedule::keyFor) already fired this session —
    // so a re-fetch mid-window never re-arms a reminder that already went
    // off.
    QSet<QString> m_firedKeys;

    QList<QTimer *> m_pendingTimers;

    QElapsedTimer m_sinceLastCycle; // throttles refreshAll()
};

#endif // REMINDERSCHEDULER_H
