#include "calendar/eventpilllabel.h"
#include "calendar/monthdaycellwidget.h"
#include "calendar/monthviewwidget.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

namespace {

MonthDayCellWidget *cellForDate(const MonthViewWidget &widget, const QDate &date)
{
    const auto cells = widget.findChildren<MonthDayCellWidget *>();
    for (auto *cell : cells) {
        if (cell->date() == date)
            return cell;
    }
    return nullptr;
}

MonthDayEventItem makeItem(const QString &eventId, const QString &calendarId, bool allDay,
                            const QDateTime &startInstant = QDateTime())
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = calendarId;
    item.title = QStringLiteral("Event %1").arg(eventId);
    item.color = QColor(Qt::red);
    item.allDay = allDay;
    item.startInstant = startInstant;
    return item;
}

} // namespace

class TestMonthViewWidget : public QObject
{
    Q_OBJECT
private slots:
    void constructorInitializesToCurrentMonthAndToday();
    void navigatingMonthsUpdatesDisplayedMonthAndLabel();
    void goToTodayResetsMonthAndSelection();
    void selectDateWithinDisplayedMonthOnlyEmitsDateSelected();
    void selectDateInAnotherMonthEmitsBothSignals();
    void clickingACellSelectsItsDate();
    void doubleClickingACellEmitsNewEventRequested();
    void newEventButtonEmitsNewEventRequestedForSelectedDate();
    void setEventsForCalendarSortsAllDayEventsBeforeTimedOnes();
    void clearEventsForCalendarRemovesOnlyThatCalendar();
    void clearAllEventsClearsEveryCalendar();
    void clickingAnEventPillSelectsItAndIgnoresARepeatClick();
    void backgroundClickClearsEventSelection();
    void rebuildDropsSelectionWhenTheEventDisappears();
    void scrollPositionRoundTrips();
};

void TestMonthViewWidget::constructorInitializesToCurrentMonthAndToday()
{
    MonthViewWidget widget;
    const QDate today = QDate::currentDate();

    QCOMPARE(widget.displayedMonth(), QDate(today.year(), today.month(), 1));
    QCOMPARE(widget.selectedDate(), today);
    QVERIFY(cellForDate(widget, today));
}

void TestMonthViewWidget::navigatingMonthsUpdatesDisplayedMonthAndLabel()
{
    MonthViewWidget widget;
    const QDate startMonth = widget.displayedMonth();
    QSignalSpy monthChangedSpy(&widget, &MonthViewWidget::displayedMonthChanged);

    widget.goToNextMonth();
    QCOMPARE(widget.displayedMonth(), startMonth.addMonths(1));
    QCOMPARE(monthChangedSpy.count(), 1);
    QCOMPARE(monthChangedSpy.first().first().toDate(), startMonth.addMonths(1));

    widget.goToPreviousMonth();
    widget.goToPreviousMonth();
    QCOMPARE(widget.displayedMonth(), startMonth.addMonths(-1));
    QCOMPARE(monthChangedSpy.count(), 3);

    auto *monthYearLabel = widget.findChild<QLabel *>(QStringLiteral("monthYearLabel"));
    QVERIFY(!monthYearLabel->text().isEmpty());
}

void TestMonthViewWidget::goToTodayResetsMonthAndSelection()
{
    MonthViewWidget widget;
    const QDate today = QDate::currentDate();
    widget.goToNextMonth();
    widget.goToNextMonth();

    QSignalSpy monthChangedSpy(&widget, &MonthViewWidget::displayedMonthChanged);
    QSignalSpy dateSelectedSpy(&widget, &MonthViewWidget::dateSelected);
    widget.goToToday();

    QCOMPARE(widget.displayedMonth(), QDate(today.year(), today.month(), 1));
    QCOMPARE(widget.selectedDate(), today);
    QCOMPARE(monthChangedSpy.count(), 1);
    QCOMPARE(dateSelectedSpy.count(), 1);
    QCOMPARE(dateSelectedSpy.first().first().toDate(), today);
}

void TestMonthViewWidget::selectDateWithinDisplayedMonthOnlyEmitsDateSelected()
{
    MonthViewWidget widget;
    const QDate today = QDate::currentDate();
    const QDate firstOfMonth(today.year(), today.month(), 1);
    // firstOfMonth is always in the displayed (current) month.
    QSignalSpy monthChangedSpy(&widget, &MonthViewWidget::displayedMonthChanged);
    QSignalSpy dateSelectedSpy(&widget, &MonthViewWidget::dateSelected);

    widget.selectDate(firstOfMonth);

    QCOMPARE(widget.selectedDate(), firstOfMonth);
    QCOMPARE(monthChangedSpy.count(), 0);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestMonthViewWidget::selectDateInAnotherMonthEmitsBothSignals()
{
    MonthViewWidget widget;
    const QDate otherMonthDate = widget.displayedMonth().addMonths(1);
    QSignalSpy monthChangedSpy(&widget, &MonthViewWidget::displayedMonthChanged);
    QSignalSpy dateSelectedSpy(&widget, &MonthViewWidget::dateSelected);

    widget.selectDate(otherMonthDate);

    QCOMPARE(widget.displayedMonth(), otherMonthDate);
    QCOMPARE(widget.selectedDate(), otherMonthDate);
    QCOMPARE(monthChangedSpy.count(), 1);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestMonthViewWidget::clickingACellSelectsItsDate()
{
    MonthViewWidget widget;
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QDate target = widget.displayedMonth().addDays(10);
    auto *cell = cellForDate(widget, target);
    QVERIFY(cell);

    QSignalSpy dateSelectedSpy(&widget, &MonthViewWidget::dateSelected);
    QTest::mouseClick(cell, Qt::LeftButton);

    QCOMPARE(widget.selectedDate(), target);
    QCOMPARE(dateSelectedSpy.count(), 1);
}

void TestMonthViewWidget::doubleClickingACellEmitsNewEventRequested()
{
    MonthViewWidget widget;
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QDate target = widget.displayedMonth().addDays(3);
    auto *cell = cellForDate(widget, target);
    QSignalSpy spy(&widget, &MonthViewWidget::newEventRequested);
    QTest::mouseDClick(cell, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDate(), target);
}

void TestMonthViewWidget::newEventButtonEmitsNewEventRequestedForSelectedDate()
{
    MonthViewWidget widget;
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QDate target = widget.displayedMonth().addDays(5);
    widget.selectDate(target);

    auto *newEventButton = widget.findChild<QPushButton *>(QStringLiteral("newEventButton"));
    QSignalSpy spy(&widget, &MonthViewWidget::newEventRequested);
    QTest::mouseClick(newEventButton, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDate(), target);
}

void TestMonthViewWidget::setEventsForCalendarSortsAllDayEventsBeforeTimedOnes()
{
    MonthViewWidget widget;
    const QDate date = widget.displayedMonth();

    const QHash<QDate, QList<MonthDayEventItem>> eventsByDate{
        {date, {makeItem(QStringLiteral("timed"), QStringLiteral("a"), false, QDateTime(date, QTime(9, 0))),
                makeItem(QStringLiteral("allday"), QStringLiteral("a"), true)}}};
    widget.setEventsForCalendar(QStringLiteral("a"), eventsByDate);

    auto *cell = cellForDate(widget, date);
    const auto pills = cell->findChildren<EventPillLabel *>();
    QCOMPARE(pills.size(), 2);
    QVERIFY(pills.first()->matchesEvent(QStringLiteral("a"), QStringLiteral("allday"))); // all-day sorts first
    QVERIFY(pills.last()->matchesEvent(QStringLiteral("a"), QStringLiteral("timed")));
}

void TestMonthViewWidget::clearEventsForCalendarRemovesOnlyThatCalendar()
{
    MonthViewWidget widget;
    const QDate date = widget.displayedMonth();

    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeItem(QStringLiteral("e1"), QStringLiteral("a"), true)}}});
    widget.setEventsForCalendar(QStringLiteral("b"), {{date, {makeItem(QStringLiteral("e2"), QStringLiteral("b"), true)}}});
    QTest::qWait(10); // each rebuild only deleteLater()'s the previous pills
    QCOMPARE(cellForDate(widget, date)->findChildren<EventPillLabel *>().size(), 2);

    widget.clearEventsForCalendar(QStringLiteral("a"));
    QTest::qWait(10); // old pills are only deleteLater()'d

    const auto remaining = cellForDate(widget, date)->findChildren<EventPillLabel *>();
    QCOMPARE(remaining.size(), 1);
    QVERIFY(remaining.first()->matchesEvent(QStringLiteral("b"), QStringLiteral("e2")));
}

void TestMonthViewWidget::clearAllEventsClearsEveryCalendar()
{
    MonthViewWidget widget;
    const QDate date = widget.displayedMonth();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeItem(QStringLiteral("e1"), QStringLiteral("a"), true)}}});

    widget.clearAllEvents();
    QTest::qWait(10);

    QCOMPARE(cellForDate(widget, date)->findChildren<EventPillLabel *>().size(), 0);
}

void TestMonthViewWidget::clickingAnEventPillSelectsItAndIgnoresARepeatClick()
{
    MonthViewWidget widget;
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate date = widget.displayedMonth();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeItem(QStringLiteral("e1"), QStringLiteral("a"), true)}}});

    auto *pill = cellForDate(widget, date)->findChild<EventPillLabel *>();
    QSignalSpy selectionSpy(&widget, &MonthViewWidget::eventSelectionChanged);
    QTest::mouseClick(pill, Qt::LeftButton);
    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.first().at(1).toString(), QStringLiteral("e1"));

    QTest::mouseClick(pill, Qt::LeftButton); // clicking the same event again: no-op
    QCOMPARE(selectionSpy.count(), 1);
}

void TestMonthViewWidget::backgroundClickClearsEventSelection()
{
    MonthViewWidget widget;
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate date = widget.displayedMonth();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeItem(QStringLiteral("e1"), QStringLiteral("a"), true)}}});
    auto *cell = cellForDate(widget, date);
    QTest::mouseClick(cell->findChild<EventPillLabel *>(), Qt::LeftButton);

    QSignalSpy selectionSpy(&widget, &MonthViewWidget::eventSelectionChanged);
    QTest::mouseClick(cell, Qt::LeftButton); // background click on the same cell

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.first().at(0).toString(), QString());
    QCOMPARE(selectionSpy.first().at(1).toString(), QString());
}

void TestMonthViewWidget::rebuildDropsSelectionWhenTheEventDisappears()
{
    MonthViewWidget widget;
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    const QDate date = widget.displayedMonth();
    widget.setEventsForCalendar(QStringLiteral("a"), {{date, {makeItem(QStringLiteral("e1"), QStringLiteral("a"), true)}}});
    QTest::mouseClick(cellForDate(widget, date)->findChild<EventPillLabel *>(), Qt::LeftButton);

    QSignalSpy selectionSpy(&widget, &MonthViewWidget::eventSelectionChanged);
    widget.setEventsForCalendar(QStringLiteral("a"), {}); // the selected event is gone

    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(selectionSpy.first().at(1).toString(), QString());
}

void TestMonthViewWidget::scrollPositionRoundTrips()
{
    MonthViewWidget widget;
    widget.resize(200, 150); // small enough that the grid likely overflows and scrolls

    widget.setScrollPosition(QPoint(0, 0));
    QCOMPARE(widget.scrollPosition(), QPoint(0, 0));

    // Whether the grid actually overflows at this size is platform-dependent
    // (font metrics etc.); only assert a real scroll if it does, but always
    // exercise the code paths above either way.
    const QPoint maxPos = widget.maxScrollPosition();
    if (maxPos.y() > 0) {
        widget.setScrollPosition(QPoint(0, maxPos.y()));
        QCOMPARE(widget.scrollPosition(), QPoint(0, maxPos.y()));
    }
}

QTEST_MAIN(TestMonthViewWidget)
#include "test_monthviewwidget.moc"
