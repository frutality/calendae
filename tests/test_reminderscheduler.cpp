#include "auth/authmanager.h"
#include "calendar/calendar.h"
#include "calendar/event.h"
#include "calendar/reminderscheduler.h"
#include "recordingapi.h"

#include <QSignalSpy>
#include <QTest>

namespace {

Calendar makeCalendar(const QString &id, bool selected)
{
    Calendar cal;
    cal.id = id;
    cal.summary = QStringLiteral("Calendar %1").arg(id);
    cal.selected = selected;
    return cal;
}

// An event whose one popup reminder fires `secsFromNow` seconds from now —
// far enough in the future to definitely not have fired yet, close enough
// that tests don't have to wait long for it.
Event makeSoonEvent(const QString &id, const QString &calendarId, int secsFromNow)
{
    Event event;
    event.id = id;
    event.calendarId = calendarId;
    event.summary = QStringLiteral("Event %1").arg(id);
    event.allDay = false;
    event.startDateTime = QDateTime::currentDateTime().addSecs(secsFromNow);
    event.endDateTime = event.startDateTime.addSecs(1800);
    event.reminderOverrides = {{QStringLiteral("popup"), 0}};
    return event;
}

} // namespace

class TestReminderScheduler : public QObject
{
    Q_OBJECT
private slots:
    void setCalendarsWhileSignedOutFetchesNothing();
    void setCalendarsWhileSignedInFetchesOnlyEnabledCalendars();
    void setCalendarEnabledFetchesNewlyEnabledCalendar();
    void setCalendarEnabledIgnoresUnknownCalendar();
    void disablingCalendarDropsItsCachedEventsAndCancelsTimers();
    void supersededReplyIsIgnored();
    void replyForDisabledCalendarIsIgnored();
    void findCachedEventLooksUpFetchedEvents();
    void refreshCalendarDropsCacheAndRefetchesOnlyIfEnabled();
    void refreshCalendarIgnoresUnknownCalendar();
    void refreshAllThrottlesRapidCalls();
    void clearResetsEverything();
    void fetchFailureIsSilent();
    void reminderFiresAtScheduledTime();
};

void TestReminderScheduler::setCalendarsWhileSignedOutFetchesNothing()
{
    AuthManager auth; // SignedOut
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});

    QCOMPARE(api.calls.size(), 0);
}

void TestReminderScheduler::setCalendarsWhileSignedInFetchesOnlyEnabledCalendars()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true), makeCalendar(QStringLiteral("b"), false)});

    QCOMPARE(api.calls.size(), 1);
    QCOMPARE(api.calls.first().calendarId, QStringLiteral("a"));
}

void TestReminderScheduler::setCalendarEnabledFetchesNewlyEnabledCalendar()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), false)});
    QCOMPARE(api.calls.size(), 0);

    scheduler.setCalendarEnabled(QStringLiteral("a"), true);
    QCOMPARE(api.calls.size(), 1);
    QCOMPARE(api.calls.first().calendarId, QStringLiteral("a"));
}

void TestReminderScheduler::setCalendarEnabledIgnoresUnknownCalendar()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendarEnabled(QStringLiteral("unknown"), true);

    QCOMPARE(api.calls.size(), 0);
}

void TestReminderScheduler::disablingCalendarDropsItsCachedEventsAndCancelsTimers()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);
    QSignalSpy dueSpy(&scheduler, &ReminderScheduler::reminderDue);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    // Fires in 1s, if the timer survives.
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("e1"), QStringLiteral("a"), 1)});
    QVERIFY(scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());

    scheduler.setCalendarEnabled(QStringLiteral("a"), false);

    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());
    QTest::qWait(1500);
    QCOMPARE(dueSpy.count(), 0); // the timer was cancelled by rebuildTimers()
}

void TestReminderScheduler::supersededReplyIsIgnored()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    const quint64 firstRequestId = api.calls.first().id;

    // A second fetch cycle supersedes the first (e.g. refreshCalendar()).
    scheduler.refreshCalendar(QStringLiteral("a"));
    QCOMPARE(api.calls.size(), 2);

    // The stale first reply must be dropped.
    api.deliver(firstRequestId, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("stale"), QStringLiteral("a"), 100)});
    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("stale")).has_value());

    // The current one is accepted.
    api.deliver(api.calls.last().id, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("fresh"), QStringLiteral("a"), 100)});
    QVERIFY(scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("fresh")).has_value());
}

void TestReminderScheduler::replyForDisabledCalendarIsIgnored()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    const quint64 requestId = api.calls.first().id;

    scheduler.setCalendarEnabled(QStringLiteral("a"), false); // disabled before the reply lands

    api.deliver(requestId, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("e1"), QStringLiteral("a"), 100)});

    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());
}

void TestReminderScheduler::findCachedEventLooksUpFetchedEvents()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("e1"), QStringLiteral("a"), 100)});

    QVERIFY(scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());
    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("missing")).has_value());
    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("other"), QStringLiteral("e1")).has_value());
}

void TestReminderScheduler::refreshCalendarDropsCacheAndRefetchesOnlyIfEnabled()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true), makeCalendar(QStringLiteral("b"), false)});
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("e1"), QStringLiteral("a"), 100)});
    QCOMPARE(api.calls.size(), 1);

    // Enabled: cache dropped immediately, and re-fetched right away.
    scheduler.refreshCalendar(QStringLiteral("a"));
    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());
    QCOMPARE(api.calls.size(), 2);

    // Known but disabled: cache logic still runs, but no new fetch.
    scheduler.refreshCalendar(QStringLiteral("b"));
    QCOMPARE(api.calls.size(), 2);
}

void TestReminderScheduler::refreshCalendarIgnoresUnknownCalendar()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    scheduler.refreshCalendar(QStringLiteral("unknown"));

    QCOMPARE(api.calls.size(), 0);
}

void TestReminderScheduler::refreshAllThrottlesRapidCalls()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);

    // Nothing to fetch yet, so the "since last cycle" clock never starts.
    scheduler.refreshAll();
    QCOMPARE(api.calls.size(), 0);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)}); // starts a cycle, arms the clock
    QCOMPARE(api.calls.size(), 1);

    scheduler.refreshAll(); // called immediately after: throttled
    QCOMPARE(api.calls.size(), 1);
}

void TestReminderScheduler::clearResetsEverything()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);
    QSignalSpy dueSpy(&scheduler, &ReminderScheduler::reminderDue);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("e1"), QStringLiteral("a"), 1)});

    scheduler.clear();

    QVERIFY(!scheduler.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());
    // A previously-known calendar is forgotten: re-enabling it is a no-op.
    scheduler.setCalendarEnabled(QStringLiteral("a"), true);
    QCOMPARE(api.calls.size(), 1); // unchanged

    QTest::qWait(1500);
    QCOMPARE(dueSpy.count(), 0); // the armed timer was cancelled by clear()
}

void TestReminderScheduler::fetchFailureIsSilent()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);
    QSignalSpy eventFetchFailedSpy(&scheduler, &ReminderScheduler::eventFetchFailed);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.failRequest(api.calls.first().id, QStringLiteral("a"), QStringLiteral("boom"), false);

    QCOMPARE(eventFetchFailedSpy.count(), 0); // background reminder fetches never surface errors
}

void TestReminderScheduler::reminderFiresAtScheduledTime()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    ReminderScheduler scheduler(&auth, &api);
    QSignalSpy dueSpy(&scheduler, &ReminderScheduler::reminderDue);

    scheduler.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeSoonEvent(QStringLiteral("e1"), QStringLiteral("a"), 1)});

    QVERIFY(dueSpy.wait(3000));
    const DueReminder reminder = dueSpy.first().first().value<DueReminder>();
    QCOMPARE(reminder.calendarId, QStringLiteral("a"));
    QCOMPARE(reminder.eventId, QStringLiteral("e1"));
}

QTEST_GUILESS_MAIN(TestReminderScheduler)
#include "test_reminderscheduler.moc"
