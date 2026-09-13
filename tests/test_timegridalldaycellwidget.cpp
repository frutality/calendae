#include "calendar/eventpilllabel.h"
#include "calendar/timegridalldaycellwidget.h"

#include <QSignalSpy>
#include <QTest>

namespace {
MonthDayEventItem makeItem(const QString &eventId, bool allDay)
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = QStringLiteral("cal1");
    item.title = QStringLiteral("Conference");
    item.color = QColor(Qt::red);
    item.allDay = allDay;
    return item;
}
} // namespace

class TestTimeGridAllDayCellWidget : public QObject
{
    Q_OBJECT
private slots:
    void setDateStoresDate();
    void minimumSizeHintUsesFixedWidthFloor();
    void setEventsFiltersOutTimedItems();
    void backgroundClickEmitsClickedAndBackgroundClicked();
    void doubleClickOnBackgroundEmitsDoubleClicked();
    void clickingAPillEmitsClickedAndEventClicked();
    void doubleClickingAPillEmitsEventEditRequested();
    void setSelectedEventIsIdempotent();
    void resizeWithNoEventsDoesNotCrash();
};

void TestTimeGridAllDayCellWidget::setDateStoresDate()
{
    TimeGridAllDayCellWidget cell;

    QVERIFY(!cell.date().isValid());
    cell.setDate(QDate(2026, 8, 15));
    QCOMPARE(cell.date(), QDate(2026, 8, 15));
}

void TestTimeGridAllDayCellWidget::minimumSizeHintUsesFixedWidthFloor()
{
    TimeGridAllDayCellWidget cell;

    QCOMPARE(cell.minimumSizeHint().width(), 20);
}

void TestTimeGridAllDayCellWidget::setEventsFiltersOutTimedItems()
{
    TimeGridAllDayCellWidget cell;
    cell.resize(400, 100);

    cell.setEvents({makeItem(QStringLiteral("e1"), true), makeItem(QStringLiteral("e2"), false)});

    const auto pills = cell.findChildren<EventPillLabel *>();
    QCOMPARE(pills.size(), 1); // the timed item is silently dropped
    QVERIFY(pills.first()->matchesEvent(QStringLiteral("cal1"), QStringLiteral("e1")));
    QCOMPARE(pills.first()->text(), QStringLiteral("Conference"));

    cell.setEvents({}); // rebuild with nothing; old pills are only deleteLater()'d
    QTest::qWait(10);
    QCOMPARE(cell.findChildren<EventPillLabel *>().size(), 0);
}

void TestTimeGridAllDayCellWidget::backgroundClickEmitsClickedAndBackgroundClicked()
{
    TimeGridAllDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.resize(200, 60);
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    QSignalSpy clickedSpy(&cell, &TimeGridAllDayCellWidget::clicked);
    QSignalSpy backgroundSpy(&cell, &TimeGridAllDayCellWidget::backgroundClicked);
    QTest::mouseClick(&cell, Qt::LeftButton);

    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(clickedSpy.first().first().toDate(), QDate(2026, 8, 15));
    QCOMPARE(backgroundSpy.count(), 1);
}

void TestTimeGridAllDayCellWidget::doubleClickOnBackgroundEmitsDoubleClicked()
{
    TimeGridAllDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.resize(200, 60);
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    QSignalSpy spy(&cell, &TimeGridAllDayCellWidget::doubleClicked);
    QTest::mouseDClick(&cell, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
}

void TestTimeGridAllDayCellWidget::clickingAPillEmitsClickedAndEventClicked()
{
    TimeGridAllDayCellWidget cell;
    cell.setDate(QDate(2026, 8, 15));
    cell.resize(400, 100);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    auto *pill = cell.findChild<EventPillLabel *>();
    QSignalSpy clickedSpy(&cell, &TimeGridAllDayCellWidget::clicked);
    QSignalSpy eventClickedSpy(&cell, &TimeGridAllDayCellWidget::eventClicked);
    QTest::mouseClick(pill, Qt::LeftButton);

    QCOMPARE(clickedSpy.count(), 1); // unlike MonthDayCellWidget, always emitted
    QCOMPARE(eventClickedSpy.count(), 1);
    QCOMPARE(eventClickedSpy.first().at(1).toString(), QStringLiteral("e1"));
}

void TestTimeGridAllDayCellWidget::doubleClickingAPillEmitsEventEditRequested()
{
    TimeGridAllDayCellWidget cell;
    cell.resize(400, 100);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});
    cell.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cell));

    auto *pill = cell.findChild<EventPillLabel *>();
    QSignalSpy editSpy(&cell, &TimeGridAllDayCellWidget::eventEditRequested);
    QTest::mouseDClick(pill, Qt::LeftButton);

    QCOMPARE(editSpy.count(), 1);
    QCOMPARE(editSpy.first().at(1).toString(), QStringLiteral("e1"));
}

void TestTimeGridAllDayCellWidget::setSelectedEventIsIdempotent()
{
    TimeGridAllDayCellWidget cell;
    cell.resize(400, 100);
    cell.setEvents({makeItem(QStringLiteral("e1"), true)});

    cell.setSelectedEvent(QStringLiteral("cal1"), QStringLiteral("e1"));
    cell.setSelectedEvent(QStringLiteral("cal1"), QStringLiteral("e1")); // unchanged: early-return
    cell.setSelectedEvent(QString(), QString());
}

void TestTimeGridAllDayCellWidget::resizeWithNoEventsDoesNotCrash()
{
    TimeGridAllDayCellWidget cell;
    cell.resize(200, 60);
    cell.resize(300, 60);
}

QTEST_MAIN(TestTimeGridAllDayCellWidget)
#include "test_timegridalldaycellwidget.moc"
