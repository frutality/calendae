#include "calendar/monthgrid.h"

#include <QTest>

class TestMonthGrid : public QObject
{
    Q_OBJECT
private slots:
    void alwaysReturns42Dates();
    void mondayStartSixRowMonth();
    void mondayStartFiveRowMonth();
    void firstDayOfWeekShiftsAlignment();
};

void TestMonthGrid::alwaysReturns42Dates()
{
    QCOMPARE(MonthGrid::datesForGrid(QDate(2026, 8, 24), Qt::Monday).size(), 42);
    QCOMPARE(MonthGrid::datesForGrid(QDate(2026, 2, 15), Qt::Sunday).size(), 42);
    QCOMPARE(MonthGrid::datesForGrid(QDate(2026, 12, 1), Qt::Monday).size(), 42);
}

void TestMonthGrid::mondayStartSixRowMonth()
{
    // August 2026: Aug 1 is a Saturday, Aug 31 is a Monday -> this month
    // needs the full 6th row of real (non-adjacent) days.
    const QList<QDate> dates = MonthGrid::datesForGrid(QDate(2026, 8, 24), Qt::Monday);
    QCOMPARE(dates.size(), 42);
    QCOMPARE(dates.first(), QDate(2026, 7, 27));
    QCOMPARE(dates.last(), QDate(2026, 9, 6));
    QVERIFY(dates.contains(QDate(2026, 8, 1)));
    QVERIFY(dates.contains(QDate(2026, 8, 31)));

    // Last row (indices 35..41) contains at least one August date.
    bool lastRowHasAugustDate = false;
    for (int i = 35; i < 42; ++i) {
        if (dates.at(i).month() == 8 && dates.at(i).year() == 2026)
            lastRowHasAugustDate = true;
    }
    QVERIFY(lastRowHasAugustDate);
}

void TestMonthGrid::mondayStartFiveRowMonth()
{
    // January 2026 only needs 5 rows of real content with a Monday start;
    // the grid must still be exactly 42 dates (stable layout).
    const QList<QDate> dates = MonthGrid::datesForGrid(QDate(2026, 1, 10), Qt::Monday);
    QCOMPARE(dates.size(), 42);
    QCOMPARE(dates.first(), QDate(2025, 12, 29));
    QCOMPARE(dates.last(), QDate(2026, 2, 8));
    QVERIFY(dates.contains(QDate(2026, 1, 1)));
    QVERIFY(dates.contains(QDate(2026, 1, 31)));

    for (int i = 35; i < 42; ++i)
        QVERIFY(dates.at(i).month() != 1 || dates.at(i).year() != 2026);
}

void TestMonthGrid::firstDayOfWeekShiftsAlignment()
{
    // February 2026: Feb 1 is a Sunday, so a Sunday-first grid starts
    // exactly on Feb 1 (zero leading days), while a Monday-first grid
    // needs 6 leading days from January.
    const QList<QDate> mondayFirst = MonthGrid::datesForGrid(QDate(2026, 2, 15), Qt::Monday);
    const QList<QDate> sundayFirst = MonthGrid::datesForGrid(QDate(2026, 2, 15), Qt::Sunday);

    QCOMPARE(mondayFirst.first(), QDate(2026, 1, 26));
    QCOMPARE(sundayFirst.first(), QDate(2026, 2, 1));
    QVERIFY(mondayFirst.first() != sundayFirst.first());
}

QTEST_APPLESS_MAIN(TestMonthGrid)
#include "test_monthgrid.moc"
