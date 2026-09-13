#include "calendar/eventpilllabel.h"
#include "calendar/monthdaycellwidget.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTest>

namespace {
MonthDayEventItem makeItem(const QString &eventId, bool allDay, const QString &timeLabel = QString())
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = QStringLiteral("cal1");
    item.title = QStringLiteral("Standup");
    item.color = QColor(Qt::red);
    item.allDay = allDay;
    item.timeLabel = timeLabel;
    return item;
}
} // namespace

class TestMonthDayCellWidget : public QObject
{
    Q_OBJECT
private slots:
    void setDateUpdatesDayNumberLabelAndIsIdempotent();
    void togglingStateFlagsIsIdempotentAndUpdatesEmphasis();
    void minimumSizeHintUsesFixedWidthFloor();
    void setEventsCreatesOnePillPerEventWithFormattedText();
    void backgroundClickEmitsClickedAndBackgroundClicked();
    void doubleClickOnBackgroundEmitsDoubleClicked();
    void clickingAPillEmitsEventClickedAndClicked();
    void clickingAPillInAFringeMonthDoesNotEmitClicked();
    void doubleClickingAPillEmitsEventEditRequested();
    void setSelectedEventIsIdempotent();
    void resizeWithNoEventsDoesNotCrash();
};

void TestMonthDayCellWidget::setDateUpdatesDayNumberLabelAndIsIdempotent()
{
    MonthDayCellWidget cell;
    auto *dayNumberLabel = cell.findChild<QLabel *>();
    QVERIFY(dayNumberLabel);

    cell.setDate(QDate(2026, 8, 15));
    QCOMPARE(cell.date(), QDate(2026, 8, 15));
    QCOMPARE(dayNumberLabel->text(), QStringLiteral("15"));

    cell.setDate(QDate(2026, 8, 15)); // same date: early-return, no crash
    QCOMPARE(dayNumberLabel->text(), QStringLiteral("15"));
}

void TestMonthDayCellWidget::togglingStateFlagsIsIdempotentAndUpdatesEmphasis()
{
    MonthDayCellWidget cell;
    auto *dayNumberLabel = cell.findChild<QLabel *>();
    cell.setDate(QDate(2026, 8, 15));

    cell.setInCurrentMonth(true); // already the default: no-op
    cell.setIsToday(false); // already the default: no-op

    cell.setIsToday(true);
    QVERIFY(dayNumberLabel->styleSheet().contains(QStringLiteral("bold")));
    cell.setIsToday(true); // unchanged: no-op

    cell.setSelected(true);
    QVERIFY(dayNumberLabel->styleSheet().contains(QStringLiteral("bold")));

    cell.setInCurrentMonth(false);
    QVERIFY(dayNumberLabel->styleSheet().contains(QStringLiteral("palette(mid)")));

    cell.setSelected(false);
    cell.setIsToday(false);
    QVERIFY(dayNumberLabel->styleSheet().contains(QStringLiteral("palette(mid)")));
    QVERIFY(!dayNumberLabel->styleSheet().contains(QStringLiteral("bold")));
}

void TestMonthDayCellWidget::minimumSizeHintUsesFixedWidthFloor()
{
    MonthDayCellWidget cell;

    QCOMPARE(cell.minimumSizeHint().width(), MonthDayCellWidget::kMinContentWidth);
}

void TestMonthDayCellWidget::setEventsCreatesOnePillPerEventWithFormattedText()
{
    MonthDayCellWidget cell;
    cell.resize(400, 200); // wide enough that elidedText() never truncates below

    cell.setEvents({makeItem(QStringLiteral("e1"), true), makeItem(QStringLiteral("e2"), false, QStringLiteral("9:00 AM"))});

    const auto pills = cell.findChildren<EventPillLabel *>();
    QCOMPARE(pills.size(), 2);
    QCOMPARE(pills.at(0)->text(), QStringLiteral("Standup")); // all-day: title only
    QCOMPARE(pills.at(1)->text(), QStringLiteral("9:00 AM Standup")); // timed: "time title"

    // Replacing the event list tears down (via deleteLater()) and rebuilds
    // the pills; let the deferred deletes actually run before counting.
    cell.setEvents({makeItem(QStringLiteral("e3"), true)});
    QTest::qWait(10);
    QCOMPARE(cell.findChildren<EventPillLabel *>().size(), 1);
}

void TestMonthDayCellWidget::backgroundClickEmitsClickedAndBackgroundClicked()
{
    MonthDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.resize(200, 100);
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    QSignalSpy clickedSpy(&cell, &MonthDayCellWidget::clicked);
    QSignalSpy backgroundSpy(&cell, &MonthDayCellWidget::backgroundClicked);
    QTest::mouseClick(&cell, Qt::LeftButton);

    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(clickedSpy.first().first().toDate(), QDate(2026, 8, 15));
    QCOMPARE(backgroundSpy.count(), 1);
}

void TestMonthDayCellWidget::doubleClickOnBackgroundEmitsDoubleClicked()
{
    MonthDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.resize(200, 100);
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    QSignalSpy spy(&cell, &MonthDayCellWidget::doubleClicked);
    QTest::mouseDClick(&cell, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toDate(), QDate(2026, 8, 15));
}

void TestMonthDayCellWidget::clickingAPillEmitsEventClickedAndClicked()
{
    MonthDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.setInCurrentMonth(true);
    cell.resize(400, 200);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    auto *pill = cell.findChild<EventPillLabel *>();
    QVERIFY(pill);

    QSignalSpy clickedSpy(&cell, &MonthDayCellWidget::clicked);
    QSignalSpy eventClickedSpy(&cell, &MonthDayCellWidget::eventClicked);
    QTest::mouseClick(pill, Qt::LeftButton);

    QCOMPARE(clickedSpy.count(), 1); // in the current month: the day highlight moves too
    QCOMPARE(eventClickedSpy.count(), 1);
    QCOMPARE(eventClickedSpy.first().at(0).toString(), QStringLiteral("cal1"));
    QCOMPARE(eventClickedSpy.first().at(1).toString(), QStringLiteral("e1"));
}

void TestMonthDayCellWidget::clickingAPillInAFringeMonthDoesNotEmitClicked()
{
    MonthDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.setInCurrentMonth(false); // a fringe (adjacent-month) cell
    cell.resize(400, 200);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    auto *pill = cell.findChild<EventPillLabel *>();
    QSignalSpy clickedSpy(&cell, &MonthDayCellWidget::clicked);
    QSignalSpy eventClickedSpy(&cell, &MonthDayCellWidget::eventClicked);
    QTest::mouseClick(pill, Qt::LeftButton);

    QCOMPARE(clickedSpy.count(), 0); // must not navigate away from a fringe cell
    QCOMPARE(eventClickedSpy.count(), 1);
}

void TestMonthDayCellWidget::doubleClickingAPillEmitsEventEditRequested()
{
    MonthDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.resize(400, 200);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    auto *pill = cell.findChild<EventPillLabel *>();
    QSignalSpy editSpy(&cell, &MonthDayCellWidget::eventEditRequested);
    QTest::mouseDClick(pill, Qt::LeftButton);

    QCOMPARE(editSpy.count(), 1);
    QCOMPARE(editSpy.first().at(1).toString(), QStringLiteral("e1"));
}

void TestMonthDayCellWidget::setSelectedEventIsIdempotent()
{
    MonthDayCellWidget cell;
    cell.resize(400, 200);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});

    cell.setSelectedEvent(QStringLiteral("cal1"), QStringLiteral("e1"));
    cell.setSelectedEvent(QStringLiteral("cal1"), QStringLiteral("e1")); // unchanged: early-return
    cell.setSelectedEvent(QString(), QString()); // clears selection
}

void TestMonthDayCellWidget::resizeWithNoEventsDoesNotCrash()
{
    MonthDayCellWidget cell;
    cell.resize(200, 100);
    cell.resize(300, 100); // no pills yet: updateEventPillTexts() must not run
}

QTEST_MAIN(TestMonthDayCellWidget)
#include "test_monthdaycellwidget.moc"
