#include "auth/authmanager.h"
#include "calendar/calendar.h"
#include "calendar/event.h"
#include "calendar/eventgridview.h"
#include "calendar/montheventstore.h"
#include "calendar/storebackedeventscontroller.h"
#include "recordingapi.h"

#include <QSignalSpy>
#include <QTest>

namespace {

// Records every call the controller makes into the view, with no real
// widget involved — StoreBackedEventsController only ever talks to
// EventGridView's three methods.
class FakeEventGridView : public EventGridView
{
public:
    struct SetCall
    {
        QString calendarId;
        int dateCount;
        int totalEvents;
    };
    QList<SetCall> setCalls;
    QStringList clearedCalendarIds;
    int clearAllCalls = 0;

    void setEventsForCalendar(const QString &calendarId, const QHash<QDate, QList<MonthDayEventItem>> &eventsByDate) override
    {
        int total = 0;
        for (const auto &list : eventsByDate)
            total += list.size();
        setCalls.append({calendarId, int(eventsByDate.size()), total});
    }
    void clearEventsForCalendar(const QString &calendarId) override { clearedCalendarIds.append(calendarId); }
    void clearAllEvents() override { ++clearAllCalls; }
};

// A minimal concrete controller: currentMonthKeys() is whatever the test
// sets, and reloadCurrentRange() (normally wired to a view's navigation
// signal by a real subclass) is exposed for the test to call directly, so no
// real MonthViewWidget/TimeGridViewWidget is needed.
class TestController : public StoreBackedEventsController
{
public:
    using StoreBackedEventsController::StoreBackedEventsController;

    QList<QDate> monthKeys;

    QList<QDate> currentMonthKeys() const override { return monthKeys; }
    void navigate() { reloadCurrentRange(); }
};

Calendar makeCalendar(const QString &id, bool selected)
{
    Calendar cal;
    cal.id = id;
    cal.summary = QStringLiteral("Calendar %1").arg(id);
    cal.selected = selected;
    return cal;
}

Event makeEvent(const QString &id, const QString &calendarId)
{
    Event event;
    event.id = id;
    event.calendarId = calendarId;
    event.allDay = true;
    event.startDate = QDate(2026, 8, 15);
    event.endDate = QDate(2026, 8, 16);
    return event;
}

const QDate kAug(2026, 8, 1);
const QDate kSep(2026, 9, 1);

} // namespace

class TestStoreBackedEventsController : public QObject
{
    Q_OBJECT
private slots:
    void setCalendarsWhileSignedOutDoesNotFetchOrRender();
    void setCalendarsWhileSignedInFetchesOnlyEnabledCalendars();
    void bucketUpdateRendersAndSettlesCycle();
    void setCalendarEnabledFetchesAndRendersNewlyEnabledCalendar();
    void setCalendarEnabledClearsViewWhenDisabled();
    void setCalendarEnabledIgnoresUnknownCalendar();
    void clearResetsStateAndClearsView();
    void refreshCalendarRequiresKnownCalendarAndReadiness();
    void refreshVisibleFromServerOnlyWhenReady();
    void findCachedEventLooksUpCurrentMonthKeys();
    void staleBucketUpdateAfterNavigatingAwayIsIgnored();
    void nonTransientFetchFailureEmitsEventFetchFailed();
    void transientFetchFailureStaysSilent();
    void nonTransientRefreshFailureEmitsEventFetchFailed();
};

void TestStoreBackedEventsController::setCalendarsWhileSignedOutDoesNotFetchOrRender()
{
    AuthManager auth; // SignedOut
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});

    QCOMPARE(api.calls.size(), 0);
    QCOMPARE(view.clearAllCalls, 0);
    QCOMPARE(view.setCalls.size(), 0);
}

void TestStoreBackedEventsController::setCalendarsWhileSignedInFetchesOnlyEnabledCalendars()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true), makeCalendar(QStringLiteral("b"), false)});

    QCOMPARE(view.clearAllCalls, 1);
    QCOMPARE(api.calls.size(), 1);
    QCOMPARE(api.calls.first().calendarId, QStringLiteral("a"));
    // Rendered from cache immediately (empty: the fetch is still in flight).
    QCOMPARE(view.setCalls.size(), 1);
    QCOMPARE(view.setCalls.first().calendarId, QStringLiteral("a"));
    QCOMPARE(view.setCalls.first().totalEvents, 0);
}

void TestStoreBackedEventsController::bucketUpdateRendersAndSettlesCycle()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};
    QSignalSpy cycleFinished(&controller, &StoreBackedEventsController::fetchCycleFinished);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    QCOMPARE(cycleFinished.count(), 0); // still in flight

    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    // eventsFetched -> MonthEventStore is a queued connection.
    QTRY_COMPARE(cycleFinished.count(), 1);

    QCOMPARE(view.setCalls.size(), 2); // initial empty render + the post-fetch render
    QCOMPARE(view.setCalls.last().totalEvents, 1);
}

void TestStoreBackedEventsController::setCalendarEnabledFetchesAndRendersNewlyEnabledCalendar()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    // Both start disabled: no fetch yet.
    controller.setCalendars({makeCalendar(QStringLiteral("a"), false)});
    QCOMPARE(api.calls.size(), 0);

    controller.setCalendarEnabled(QStringLiteral("a"), true);
    QCOMPARE(api.calls.size(), 1);
    QCOMPARE(api.calls.first().calendarId, QStringLiteral("a"));
}

void TestStoreBackedEventsController::setCalendarEnabledClearsViewWhenDisabled()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    controller.setCalendarEnabled(QStringLiteral("a"), false);

    QCOMPARE(view.clearedCalendarIds, QStringList{QStringLiteral("a")});
}

void TestStoreBackedEventsController::setCalendarEnabledIgnoresUnknownCalendar()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendarEnabled(QStringLiteral("unknown"), true);

    QCOMPARE(api.calls.size(), 0);
}

void TestStoreBackedEventsController::clearResetsStateAndClearsView()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    controller.clear();

    QCOMPARE(view.clearAllCalls, 2); // once from setCalendars' reload, once from clear()

    // Calendar set was forgotten: re-enabling a previously-known calendar is
    // now a no-op, same as an unknown one.
    controller.setCalendarEnabled(QStringLiteral("a"), true);
    QCOMPARE(api.calls.size(), 1); // only the original setCalendars() fetch
}

void TestStoreBackedEventsController::refreshCalendarRequiresKnownCalendarAndReadiness()
{
    AuthManager auth; // SignedOut
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.refreshCalendar(QStringLiteral("a")); // unknown + not ready: no-op either way
    QCOMPARE(api.calls.size(), 0);

    auth.setSignedInForTesting(QStringLiteral("token"));
    controller.setCalendars({makeCalendar(QStringLiteral("a"), false)}); // known, disabled
    QCOMPARE(api.calls.size(), 0);

    // refreshCalendar only checks "known + ready", not "enabled" — even a
    // disabled calendar is re-fetched (the store just won't be asked to
    // render it, since renderCalendarFromCache() itself checks enabled).
    controller.refreshCalendar(QStringLiteral("a"));
    QCOMPARE(api.calls.size(), 1);

    controller.refreshCalendar(QStringLiteral("unknown"));
    QCOMPARE(api.calls.size(), 1); // unchanged
}

void TestStoreBackedEventsController::refreshVisibleFromServerOnlyWhenReady()
{
    AuthManager auth; // SignedOut
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.refreshVisibleFromServer();
    QCOMPARE(api.calls.size(), 0);

    auth.setSignedInForTesting(QStringLiteral("token"));
    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    QCOMPARE(api.calls.size(), 1);
    // refreshVisible() skips a still-in-flight bucket (see MonthEventStore),
    // so let this one land first.
    api.deliver(api.calls.first().id, QStringLiteral("a"), {});
    QTest::qWait(50);

    controller.refreshVisibleFromServer();
    QCOMPARE(api.calls.size(), 2); // re-hits the network unconditionally
}

void TestStoreBackedEventsController::findCachedEventLooksUpCurrentMonthKeys()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};
    QSignalSpy cycleFinished(&controller, &StoreBackedEventsController::fetchCycleFinished);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    QTRY_COMPARE(cycleFinished.count(), 1);

    QVERIFY(controller.findCachedEvent(QStringLiteral("a"), QStringLiteral("e1")).has_value());
    QVERIFY(!controller.findCachedEvent(QStringLiteral("a"), QStringLiteral("missing")).has_value());
    QVERIFY(!controller.findCachedEvent(QStringLiteral("other"), QStringLiteral("e1")).has_value());
}

void TestStoreBackedEventsController::staleBucketUpdateAfterNavigatingAwayIsIgnored()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    const quint64 requestId = api.calls.first().id;
    QCOMPARE(view.setCalls.size(), 1); // initial empty render only

    // The view navigated to a different month before the reply landed.
    controller.monthKeys = {kSep};

    api.deliver(requestId, QStringLiteral("a"), {makeEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    // Give the queued MonthEventStore connection a chance to run, then
    // confirm the controller's own staleness check dropped it.
    QTest::qWait(50);
    QCOMPARE(view.setCalls.size(), 1); // no new render call
}

void TestStoreBackedEventsController::nonTransientFetchFailureEmitsEventFetchFailed()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};
    QSignalSpy failedSpy(&controller, &StoreBackedEventsController::eventFetchFailed);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.failRequest(api.calls.first().id, QStringLiteral("a"), QStringLiteral("boom"), false);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().first().toString().contains(QStringLiteral("Calendar a")));
    QVERIFY(failedSpy.first().first().toString().contains(QStringLiteral("boom")));
}

void TestStoreBackedEventsController::transientFetchFailureStaysSilent()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};
    QSignalSpy failedSpy(&controller, &StoreBackedEventsController::eventFetchFailed);
    QSignalSpy cycleFinished(&controller, &StoreBackedEventsController::fetchCycleFinished);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.failRequest(api.calls.first().id, QStringLiteral("a"), QStringLiteral("offline"), true);

    QVERIFY(cycleFinished.wait()); // the cycle still settles...
    QCOMPARE(failedSpy.count(), 0); // ...but nothing is emitted for a transient failure
}

void TestStoreBackedEventsController::nonTransientRefreshFailureEmitsEventFetchFailed()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    FakeEventGridView view;
    TestController controller(&auth, &store, &view);
    controller.monthKeys = {kAug};

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    api.deliver(api.calls.first().id, QStringLiteral("a"), {makeEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    QTest::qWait(50);

    controller.refreshVisibleFromServer(); // new in-flight request over existing events
    QCOMPARE(api.calls.size(), 2);

    QSignalSpy failedSpy(&controller, &StoreBackedEventsController::eventFetchFailed);
    api.failRequest(api.calls.last().id, QStringLiteral("a"), QStringLiteral("server error"), false);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().first().toString().contains(QStringLiteral("Could not refresh events")));
}

QTEST_GUILESS_MAIN(TestStoreBackedEventsController)
#include "test_storebackedeventscontroller.moc"
