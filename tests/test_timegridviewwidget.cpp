#include "calendar/eventpilllabel.h"
#include "calendar/timegridalldaycellwidget.h"
#include "calendar/timegriddaycolumnwidget.h"
#include "calendar/timegridrange.h"
#include "calendar/timegridviewwidget.h"

#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <QToolButton>

namespace {

TimeGridAllDayCellWidget *allDayCellForDate(const TimeGridViewWidget &widget, const QDate &date)
{
    for (auto *cell : widget.findChildren<TimeGridAllDayCellWidget *>()) {
        if (cell->date() == date)
            return cell;
    }
    return nullptr;
}

TimeGridDayColumnWidget *dayColumnForDate(const TimeGridViewWidget &widget, const QDate &date)
{
    for (auto *column : widget.findChildren<TimeGridDayColumnWidget *>()) {
        if (column->date() == date)
            return column;
    }
    return nullptr;
}

// The per-day header buttons have no object name (unlike prevRangeButton/
// nextRangeButton, set by the .ui file), so filtering those two out leaves
// exactly dayCount() buttons, in range order.
QList<QToolButton *> dayHeaderButtons(const TimeGridViewWidget &widget)
{
    QList<QToolButton *> result;
    for (auto *button : widget.findChildren<QToolButton *>()) {
        if (button->objectName() != QStringLiteral("prevRangeButton") && button->objectName() != QStringLiteral("nextRangeButton"))
            result.append(button);
    }
    return result;
}

MonthDayEventItem makeAllDayItem(const QString &eventId, const QString &calendarId)
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = calendarId;
    item.title = QStringLiteral("All day");
    item.allDay = true;
    return item;
}

MonthDayEventItem makeTimedItem(const QString &eventId, const QString &calendarId, const QDateTime &start,
                                 const QDateTime &end)
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = calendarId;
    item.title = QStringLiteral("Timed");
    item.allDay = false;
    item.startInstant = start;
    item.endInstant = end;
    return item;
}

} // namespace

class TestTimeGridViewWidget : public QObject
{
    Q_OBJECT
private slots:
    void dayModeConstructorInitializesToToday();
    void weekModeConstructorInitializesToWeekStart();
    void goToPreviousAndNextShiftRangeByDayCount();
    void goToTodayResetsRangeAndSelection();
    void selectDateWithinWeekOnlyEmitsDateSelected();
    void selectDateInAnotherWeekEmitsBothSignals();
    void clickingHeaderButtonSelectsThatColumnsDate();
    void clickingAllDayCellSelectsItsDate();
    void doubleClickingAllDayCellEmitsNewEventRequested();
    void doubleClickingDayColumnEmitsNewTimedEventRequested();
    void newEventButtonEmitsNewEventRequestedForSelectedDate();
    void setEventsForCalendarSplitsAllDayAndTimedAcrossWidgets();
    void clearEventsForCalendarRemovesOnlyThatCalendar();
    void clearAllEventsClearsEveryCalendar();
    void eventSelectionAppliesAcrossAllDayCellAndDayColumnAndIgnoresRepeatClick();
    void backgroundClickClearsEventSelection();
    void rebuildDropsSelectionWhenTheEventDisappears();
    void scrollPositionRoundTrips();
};

void TestTimeGridViewWidget::dayModeConstructorInitializesToToday()
{
    TimeGridViewWidget widget(1);
    const QDate today = QDate::currentDate();

    QCOMPARE(widget.dayCount(), 1);
    QCOMPARE(widget.rangeStart(), today);
    QCOMPARE(widget.selectedDate(), today);
}

void TestTimeGridViewWidget::weekModeConstructorInitializesToWeekStart()
{
    TimeGridViewWidget widget(7);
    const QDate today = QDate::currentDate();
    const QDate expectedStart = TimeGridRange::weekStart(today, QLocale::system().firstDayOfWeek());

    QCOMPARE(widget.dayCount(), 7);
    QCOMPARE(widget.rangeStart(), expectedStart);
    QCOMPARE(widget.selectedDate(), today);
}

void TestTimeGridViewWidget::goToPreviousAndNextShiftRangeByDayCount()
{
    TimeGridViewWidget widget(7);
    const QDate startRange = widget.rangeStart();
    QSignalSpy rangeChangedSpy(&widget, &TimeGridViewWidget::displayedRangeChanged);

    widget.goToNext();
    QCOMPARE(widget.rangeStart(), startRange.addDays(7));
    QCOMPARE(rangeChangedSpy.count(), 1);

    widget.goToPrevious();
    QCOMPARE(widget.rangeStart(), startRange);
    QCOMPARE(rangeChangedSpy.count(), 2);

    auto *rangeLabel = widget.findChild<QLabel *>(QStringLiteral("rangeLabel"));
    QVERIFY(!rangeLabel->text().isEmpty());
}

void TestTimeGridViewWidget::goToTodayResetsRangeAndSelection()
{
    TimeGridViewWidget widget(7);
    const QDate today = QDate::currentDate();
    widget.goToNext();
    widget.goToNext();

    QSignalSpy rangeChangedSpy(&widget, &TimeGridViewWidget::displayedRangeChanged);
    QSignalSpy dateSelectedSpy(&widget, &TimeGridViewWidget::dateSelected);
    widget.goToToday();

    QCOMPARE(widget.rangeStart(), TimeGridRange::weekStart(today, QLocale::system().firstDayOfWeek()));
    QCOMPARE(widget.selectedDate(), today);
    QCOMPARE(rangeChangedSpy.count(), 1);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestTimeGridViewWidget::selectDateWithinWeekOnlyEmitsDateSelected()
{
    TimeGridViewWidget widget(7);
    const QDate otherDayInWeek = widget.rangeStart().addDays(2);
    QSignalSpy rangeChangedSpy(&widget, &TimeGridViewWidget::displayedRangeChanged);
    QSignalSpy dateSelectedSpy(&widget, &TimeGridViewWidget::dateSelected);

    widget.selectDate(otherDayInWeek);

    QCOMPARE(widget.selectedDate(), otherDayInWeek);
    QCOMPARE(rangeChangedSpy.count(), 0);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestTimeGridViewWidget::selectDateInAnotherWeekEmitsBothSignals()
{
    TimeGridViewWidget widget(7);
    const QDate nextWeekDate = widget.rangeStart().addDays(7);
    QSignalSpy rangeChangedSpy(&widget, &TimeGridViewWidget::displayedRangeChanged);
    QSignalSpy dateSelectedSpy(&widget, &TimeGridViewWidget::dateSelected);

    widget.selectDate(nextWeekDate);

    QCOMPARE(widget.rangeStart(), nextWeekDate);
    QCOMPARE(widget.selectedDate(), nextWeekDate);
    QCOMPARE(rangeChangedSpy.count(), 1);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestTimeGridViewWidget::clickingHeaderButtonSelectsThatColumnsDate()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const auto buttons = dayHeaderButtons(widget);
    QCOMPARE(buttons.size(), 7);

    QSignalSpy dateSelectedSpy(&widget, &TimeGridViewWidget::dateSelected);
    QTest::mouseClick(buttons.at(3), Qt::LeftButton);

    QCOMPARE(widget.selectedDate(), widget.rangeStart().addDays(3));
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestTimeGridViewWidget::clickingAllDayCellSelectsItsDate()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QDate target = widget.rangeStart().addDays(2);
    auto *cell = allDayCellForDate(widget, target);
    QVERIFY(cell);

    QSignalSpy dateSelectedSpy(&widget, &TimeGridViewWidget::dateSelected);
    QTest::mouseClick(cell, Qt::LeftButton);

    QCOMPARE(widget.selectedDate(), target);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestTimeGridViewWidget::doubleClickingAllDayCellEmitsNewEventRequested()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QDate target = widget.rangeStart().addDays(1);
    auto *cell = allDayCellForDate(widget, target);
    QSignalSpy spy(&widget, &TimeGridViewWidget::newEventRequested);
    QTest::mouseDClick(cell, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDate(), target);
}

void TestTimeGridViewWidget::doubleClickingDayColumnEmitsNewTimedEventRequested()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QDate target = widget.rangeStart().addDays(1);
    auto *column = dayColumnForDate(widget, target);
    QSignalSpy spy(&widget, &TimeGridViewWidget::newTimedEventRequested);
    // y=615 -> 10:15, snapped down to the containing half-hour, 10:00.
    QTest::mouseDClick(column, Qt::LeftButton, Qt::NoModifier, QPoint(10, 615));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDateTime(), QDateTime(target, QTime(10, 0), QTimeZone(QTimeZone::LocalTime)));
}

void TestTimeGridViewWidget::newEventButtonEmitsNewEventRequestedForSelectedDate()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate target = widget.rangeStart().addDays(4);
    widget.selectDate(target);

    auto *newEventButton = widget.findChild<QPushButton *>(QStringLiteral("newEventButton"));
    QSignalSpy spy(&widget, &TimeGridViewWidget::newEventRequested);
    QTest::mouseClick(newEventButton, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDate(), target);
}

void TestTimeGridViewWidget::setEventsForCalendarSplitsAllDayAndTimedAcrossWidgets()
{
    TimeGridViewWidget widget(7);
    const QDate date = widget.rangeStart();

    widget.setEventsForCalendar(QStringLiteral("a"),
                                 {{date,
                                   {makeAllDayItem(QStringLiteral("ad1"), QStringLiteral("a")),
                                    makeTimedItem(QStringLiteral("t1"), QStringLiteral("a"),
                                                  QDateTime(date, QTime(9, 0)), QDateTime(date, QTime(9, 30)))}}});

    const auto allDayPills = allDayCellForDate(widget, date)->findChildren<EventPillLabel *>();
    QCOMPARE(allDayPills.size(), 1);
    QVERIFY(allDayPills.first()->matchesEvent(QStringLiteral("a"), QStringLiteral("ad1")));

    const auto timedPills = dayColumnForDate(widget, date)->findChildren<EventPillLabel *>();
    QCOMPARE(timedPills.size(), 1);
    QVERIFY(timedPills.first()->matchesEvent(QStringLiteral("a"), QStringLiteral("t1")));
}

void TestTimeGridViewWidget::clearEventsForCalendarRemovesOnlyThatCalendar()
{
    TimeGridViewWidget widget(7);
    const QDate date = widget.rangeStart();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeAllDayItem(QStringLiteral("e1"), QStringLiteral("a"))}}});
    widget.setEventsForCalendar(QStringLiteral("b"), {{date, {makeAllDayItem(QStringLiteral("e2"), QStringLiteral("b"))}}});
    QTest::qWait(10);
    QCOMPARE(allDayCellForDate(widget, date)->findChildren<EventPillLabel *>().size(), 2);

    widget.clearEventsForCalendar(QStringLiteral("a"));
    QTest::qWait(10);

    const auto remaining = allDayCellForDate(widget, date)->findChildren<EventPillLabel *>();
    QCOMPARE(remaining.size(), 1);
    QVERIFY(remaining.first()->matchesEvent(QStringLiteral("b"), QStringLiteral("e2")));
}

void TestTimeGridViewWidget::clearAllEventsClearsEveryCalendar()
{
    TimeGridViewWidget widget(7);
    const QDate date = widget.rangeStart();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeAllDayItem(QStringLiteral("e1"), QStringLiteral("a"))}}});

    widget.clearAllEvents();
    QTest::qWait(10);

    QCOMPARE(allDayCellForDate(widget, date)->findChildren<EventPillLabel *>().size(), 0);
}

void TestTimeGridViewWidget::eventSelectionAppliesAcrossAllDayCellAndDayColumnAndIgnoresRepeatClick()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate date = widget.rangeStart();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeAllDayItem(QStringLiteral("e1"), QStringLiteral("a"))}}});

    auto *pill = allDayCellForDate(widget, date)->findChild<EventPillLabel *>();
    QSignalSpy selectionSpy(&widget, &TimeGridViewWidget::eventSelectionChanged);
    QTest::mouseClick(pill, Qt::LeftButton);

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.first().at(1).toString(), QStringLiteral("e1"));

    QTest::mouseClick(pill, Qt::LeftButton); // same event again: no-op
    QCOMPARE(selectionSpy.count(), 1);
}

void TestTimeGridViewWidget::backgroundClickClearsEventSelection()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate date = widget.rangeStart();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeAllDayItem(QStringLiteral("e1"), QStringLiteral("a"))}}});
    auto *cell = allDayCellForDate(widget, date);
    QTest::mouseClick(cell->findChild<EventPillLabel *>(), Qt::LeftButton);

    QSignalSpy selectionSpy(&widget, &TimeGridViewWidget::eventSelectionChanged);
    QTest::mouseClick(cell, Qt::LeftButton); // background click on the same cell

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.first().at(0).toString(), QString());
    QCOMPARE(selectionSpy.first().at(1).toString(), QString());
}

void TestTimeGridViewWidget::rebuildDropsSelectionWhenTheEventDisappears()
{
    TimeGridViewWidget widget(7);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate date = widget.rangeStart();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeAllDayItem(QStringLiteral("e1"), QStringLiteral("a"))}}});
    QTest::mouseClick(allDayCellForDate(widget, date)->findChild<EventPillLabel *>(), Qt::LeftButton);

    QSignalSpy selectionSpy(&widget, &TimeGridViewWidget::eventSelectionChanged);
    widget.setEventsForCalendar(QStringLiteral("a"), {}); // the selected event is gone

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.first().at(1).toString(), QString());
}

void TestTimeGridViewWidget::scrollPositionRoundTrips()
{
    TimeGridViewWidget widget(7);

    widget.setScrollPosition(QPoint(0, 100));
    QCOMPARE(widget.scrollPosition(), QPoint(0, 100));
    QCOMPARE(widget.scrollPosition().x(), 0); // horizontal scrolling is always off
}

QTEST_MAIN(TestTimeGridViewWidget)
#include "test_timegridviewwidget.moc"
