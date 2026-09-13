#include "auth/authmanager.h"
#include "calendar/calendar.h"
#include "calendar/montheventscontroller.h"
#include "calendar/montheventstore.h"
#include "calendar/monthviewwidget.h"
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

// MonthEventsController itself is thin wiring over StoreBackedEventsController
// (already covered by test_storebackedeventscontroller.cpp's fake-view
// tests); this just checks the two things genuinely specific to it: which
// month(s) it asks the store for, and that it reloads on the real
// MonthViewWidget's navigation signal.
class TestMonthEventsController : public QObject
{
    Q_OBJECT
private slots:
    void fetchesTheCurrentlyDisplayedMonthOnSetCalendars();
    void navigatingTheMonthViewReloadsTheNewMonth();
};

void TestMonthEventsController::fetchesTheCurrentlyDisplayedMonthOnSetCalendars()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    MonthViewWidget monthView;
    MonthEventsController controller(&auth, &store, &monthView);

    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});

    QCOMPARE(api.calls.size(), 1);
    QCOMPARE(api.calls.first().calendarId, QStringLiteral("a"));
}

void TestMonthEventsController::navigatingTheMonthViewReloadsTheNewMonth()
{
    AuthManager auth;
    auth.setSignedInForTesting(QStringLiteral("token"));
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    MonthViewWidget monthView;
    MonthEventsController controller(&auth, &store, &monthView);
    controller.setCalendars({makeCalendar(QStringLiteral("a"), true)});
    QCOMPARE(api.calls.size(), 1);

    monthView.goToNextMonth(); // emits displayedMonthChanged

    QCOMPARE(api.calls.size(), 2); // reloaded for the newly displayed month
}

QTEST_MAIN(TestMonthEventsController)
#include "test_montheventscontroller.moc"
