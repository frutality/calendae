#include "auth/authmanager.h"
#include "calendar/calendar.h"
#include "calendar/montheventstore.h"
#include "calendar/timegrideventscontroller.h"
#include "calendar/timegridviewwidget.h"
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
} // namespace

// TimeGridEventsController is thin wiring over StoreBackedEventsController
// (already covered by test_storebackedeventscontroller.cpp's fake-view
// tests); this just checks what's genuinely specific to it: which month(s)
// the visible range maps to, for both week and day mode, and that it
// reloads on the real TimeGridViewWidget's navigation signal.
class TestTimeGridEventsController : public QObject
{
    Q_OBJECT
private slots:
    void fetchesOnSetCalendarsInWeekMode();
    void fetchesOnSetCalendarsInDayMode();
    void navigatingTheTimeGridReloads();
};

void TestTimeGridEventsController::fetchesOnSetCalendarsInWeekMode()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    TimeGridViewWidget weekView(7);
    TimeGridEventsController controller(&auth, &store, &weekView);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});

    // A week can straddle a month boundary, needing one or two month buckets.
    QVERIFY(api.calls.size() >= 1);
    QCOMPARE(api.calls.first().calendarId, QStringLiteral("a"));
}

void TestTimeGridEventsController::fetchesOnSetCalendarsInDayMode()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    TimeGridViewWidget dayView(1);
    TimeGridEventsController controller(&auth, &store, &dayView);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});

    QCOMPARE(api.calls.size(), 1); // a single day always fits in one month bucket
}

void TestTimeGridEventsController::navigatingTheTimeGridReloads()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    TimeGridViewWidget dayView(1);
    TimeGridEventsController controller(&auth, &store, &dayView);
    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    const int initialCalls = api.calls.size();

    // Jump far enough that the new range lands in a different month bucket
    // (goToNext() alone might land in the same month as today, which would
    // still be InFlight and correctly skip a re-fetch).
    dayView.selectDate(QDate::currentDate().addMonths(2)); // emits displayedRangeChanged

    QVERIFY(api.calls.size() > initialCalls);
}

QTEST_MAIN(TestTimeGridEventsController)
#include "test_timegrideventscontroller.moc"
