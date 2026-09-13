#include "calendar/eventpilllabel.h"
#include "calendar/timegriddaycolumnwidget.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

namespace {

MonthDayEventItem makeTimedItem(const QString &eventId, const QDateTime &start, const QDateTime &end)
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = QStringLiteral("cal1");
    item.title = QStringLiteral("Event %1").arg(eventId);
    item.color = QColor(Qt::red);
    item.allDay = false;
    item.startInstant = start;
    item.endInstant = end;
    return item;
}

MonthDayEventItem makeAllDayItem(const QString &eventId)
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = QStringLiteral("cal1");
    item.title = QStringLiteral("All day");
    item.allDay = true;
    return item;
}

QDateTime localDateTime(const QDate &date, const QTime &time)
{
    return QDateTime(date, time, QTimeZone(QTimeZone::LocalTime));
}

// The current-time overlay is an anonymous-namespace private type, so it
// can't be matched by type from a test — but it's always the sole direct
// child that isn't an event pill.
QWidget *nowLineOverlayOf(const TimeGridDayColumnWidget &column)
{
    const auto children = column.findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) {
        if (!qobject_cast<EventPillLabel *>(child))
            return child;
    }
    return nullptr;
}

} // namespace

class TestTimeGridDayColumnWidget : public QObject
{
    Q_OBJECT
private slots:
    void constructorFixesHeightToWholeDay();
    void setDateIsIdempotent();
    void setIsTodayTogglesNowLineOverlayVisibility();
    void setEventsPositionsPillByClockTimeAndFiltersAllDay();
    void overlappingEventsAreSplitIntoSideBySideColumns();
    void eventIsClippedToTheDayBoundary();
    void backgroundClickEmitsClickedAndBackgroundClicked();
    void doubleClickEmitsSlotDoubleClickedSnappedToHalfHour();
    void clickingAPillEmitsClickedAndEventClicked();
    void doubleClickingAPillEmitsEventEditRequested();
    void setSelectedEventIsIdempotent();
    void refreshNowLineIsSafeRegardlessOfIsToday();
    void resizeRelayoutsPillsOrUpdatesNowLine();
};

void TestTimeGridDayColumnWidget::constructorFixesHeightToWholeDay()
{
    TimeGridDayColumnWidget column;

    QCOMPARE(column.height(), TimeGridDayColumnWidget::kDayHeight);
    QCOMPARE(column.sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(column.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
}

void TestTimeGridDayColumnWidget::setDateIsIdempotent()
{
    TimeGridDayColumnWidget column;

    QVERIFY(!column.date().isValid());
    column.setDate(QDate(2026, 8, 15));
    QCOMPARE(column.date(), QDate(2026, 8, 15));
    column.setDate(QDate(2026, 8, 15)); // no-op, must not crash
}

void TestTimeGridDayColumnWidget::setIsTodayTogglesNowLineOverlayVisibility()
{
    TimeGridDayColumnWidget column;
    column.setDate(QDate::currentDate());
    auto *overlay = nowLineOverlayOf(column);
    QVERIFY(overlay);
    QVERIFY(overlay->isHidden());

    column.setIsToday(true);
    QVERIFY(!overlay->isHidden());

    column.setIsToday(true); // unchanged: early-return, no crash
    column.setIsToday(false);
    QVERIFY(overlay->isHidden());
}

void TestTimeGridDayColumnWidget::setEventsPositionsPillByClockTimeAndFiltersAllDay()
{
    TimeGridDayColumnWidget column;
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    const QDate date(2026, 8, 15);
    column.setDate(date);

    column.setEvents({makeAllDayItem(QStringLiteral("ad1")),
                       makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(9, 30)))});

    const auto pills = column.findChildren<EventPillLabel *>();
    QCOMPARE(pills.size(), 1); // the all-day item is silently ignored here
    auto *pill = pills.first();
    QVERIFY(pill->matchesEvent(QStringLiteral("cal1"), QStringLiteral("t1")));

    const int expectedY = 9 * 60 * TimeGridDayColumnWidget::kSlotHeight / 30; // 9:00 in half-hour-row pixels
    QCOMPARE(pill->y(), expectedY);
    QVERIFY(pill->height() >= TimeGridDayColumnWidget::kMinEventHeight);
}

void TestTimeGridDayColumnWidget::overlappingEventsAreSplitIntoSideBySideColumns()
{
    TimeGridDayColumnWidget column;
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    const QDate date(2026, 8, 15);
    column.setDate(date);

    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(10, 0))),
                       makeTimedItem(QStringLiteral("t2"), localDateTime(date, QTime(9, 30)), localDateTime(date, QTime(10, 30)))});

    const auto pills = column.findChildren<EventPillLabel *>();
    QCOMPARE(pills.size(), 2);
    // Two overlapping events split the 200px width into two ~100px columns.
    QCOMPARE(pills.at(0)->x(), 0);
    QCOMPARE(pills.at(1)->x(), 100);
}

void TestTimeGridDayColumnWidget::eventIsClippedToTheDayBoundary()
{
    TimeGridDayColumnWidget column;
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    const QDate date(2026, 8, 15);
    column.setDate(date);

    // Starts the previous evening, ends an hour into this day.
    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date.addDays(-1), QTime(23, 0)),
                                     localDateTime(date, QTime(1, 0)))});

    auto *pill = column.findChild<EventPillLabel *>();
    QVERIFY(pill);
    QCOMPARE(pill->y(), 0); // clipped to this day's midnight
}

void TestTimeGridDayColumnWidget::backgroundClickEmitsClickedAndBackgroundClicked()
{
    TimeGridDayColumnWidget column;
    column.setDate(QDate(2026, 8, 15));
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    column.show();
    QVERIFY(QTest::qWaitForWindowExposed(&column));

    QSignalSpy clickedSpy(&column, &TimeGridDayColumnWidget::clicked);
    QSignalSpy backgroundSpy(&column, &TimeGridDayColumnWidget::backgroundClicked);
    QTest::mouseClick(&column, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));

    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(clickedSpy.first().first().toDate(), QDate(2026, 8, 15));
    QCOMPARE(backgroundSpy.count(), 1);
}

void TestTimeGridDayColumnWidget::doubleClickEmitsSlotDoubleClickedSnappedToHalfHour()
{
    TimeGridDayColumnWidget column;
    const QDate date(2026, 8, 15);
    column.setDate(date);
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    column.show();
    QVERIFY(QTest::qWaitForWindowExposed(&column));

    QSignalSpy spy(&column, &TimeGridDayColumnWidget::slotDoubleClicked);
    // y=615 -> 10:15, which snaps DOWN to the containing half-hour, 10:00.
    QTest::mouseDClick(&column, Qt::LeftButton, Qt::NoModifier, QPoint(10, 615));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDateTime(), localDateTime(date, QTime(10, 0)));
}

void TestTimeGridDayColumnWidget::clickingAPillEmitsClickedAndEventClicked()
{
    TimeGridDayColumnWidget column;
    const QDate date(2026, 8, 15);
    column.setDate(date);
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(9, 30)))});
    column.show();
    QVERIFY(QTest::qWaitForWindowExposed(&column));

    auto *pill = column.findChild<EventPillLabel *>();
    QSignalSpy clickedSpy(&column, &TimeGridDayColumnWidget::clicked);
    QSignalSpy eventClickedSpy(&column, &TimeGridDayColumnWidget::eventClicked);
    QTest::mouseClick(pill, Qt::LeftButton);

    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(eventClickedSpy.count(), 1);
    QCOMPARE(eventClickedSpy.first().at(1).toString(), QStringLiteral("t1"));
}

void TestTimeGridDayColumnWidget::doubleClickingAPillEmitsEventEditRequested()
{
    TimeGridDayColumnWidget column;
    const QDate date(2026, 8, 15);
    column.setDate(date);
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(9, 30)))});
    column.show();
    QVERIFY(QTest::qWaitForWindowExposed(&column));

    auto *pill = column.findChild<EventPillLabel *>();
    QSignalSpy editSpy(&column, &TimeGridDayColumnWidget::eventEditRequested);
    QTest::mouseDClick(pill, Qt::LeftButton);

    QCOMPARE(editSpy.count(), 1);
    QCOMPARE(editSpy.first().at(1).toString(), QStringLiteral("t1"));
}

void TestTimeGridDayColumnWidget::setSelectedEventIsIdempotent()
{
    TimeGridDayColumnWidget column;
    const QDate date(2026, 8, 15);
    column.setDate(date);
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(9, 30)))});

    column.setSelectedEvent(QStringLiteral("cal1"), QStringLiteral("t1"));
    column.setSelectedEvent(QStringLiteral("cal1"), QStringLiteral("t1")); // unchanged: early-return
    column.setSelectedEvent(QString(), QString());

    // The selection must survive a relayout (re-applied per pill).
    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(9, 30)))});
}

void TestTimeGridDayColumnWidget::refreshNowLineIsSafeRegardlessOfIsToday()
{
    TimeGridDayColumnWidget column;
    column.setDate(QDate::currentDate());

    column.refreshNowLine(); // isToday == false: cheap no-op
    column.setIsToday(true);
    column.refreshNowLine(); // isToday == true: repositions the line
}

void TestTimeGridDayColumnWidget::resizeRelayoutsPillsOrUpdatesNowLine()
{
    // resizeEvent() is only actually delivered to a widget that's shown
    // (or a visible ancestor's layout resizes it) — a hidden widget's
    // resize() updates its geometry but never fires the event.
    TimeGridDayColumnWidget column;
    const QDate date(2026, 8, 15);
    column.setDate(date);
    column.resize(200, TimeGridDayColumnWidget::kDayHeight);
    column.show();
    QVERIFY(QTest::qWaitForWindowExposed(&column));

    column.resize(300, TimeGridDayColumnWidget::kDayHeight); // no pills yet: just the now-line branch

    column.setEvents({makeTimedItem(QStringLiteral("t1"), localDateTime(date, QTime(9, 0)), localDateTime(date, QTime(9, 30)))});
    column.resize(400, TimeGridDayColumnWidget::kDayHeight); // pills present: relayout branch
    // On a shown top-level widget, the platform can deliver resizeEvent()
    // asynchronously after resize() rather than within the call itself.
    QTest::qWait(50);

    auto *pill = column.findChild<EventPillLabel *>();
    QCOMPARE(pill->width(), 400 - 2); // full width minus the column gap, single column
}

QTEST_MAIN(TestTimeGridDayColumnWidget)
#include "test_timegriddaycolumnwidget.moc"
