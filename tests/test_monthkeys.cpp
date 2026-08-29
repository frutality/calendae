#include "calendar/monthkeys.h"

#include <QTest>

class TestMonthKeys : public QObject
{
    Q_OBJECT
private slots:
    void normalizeToFirstOfMonth();
    void singleDayIsOneMonth();
    void rangeWithinOneMonth();
    void rangeSpanningTwoMonths();
    void rangeSpanningWholeMonthGrid();
    void rangeSpanningYearBoundary();
    void invalidOrReversedRangeIsEmpty();
};

void TestMonthKeys::normalizeToFirstOfMonth()
{
    QCOMPARE(MonthKeys::normalize(QDate(2026, 8, 29)), QDate(2026, 8, 1));
    QCOMPARE(MonthKeys::normalize(QDate(2026, 8, 1)), QDate(2026, 8, 1));
    QVERIFY(!MonthKeys::normalize(QDate()).isValid());
}

void TestMonthKeys::singleDayIsOneMonth()
{
    const QList<QDate> months = MonthKeys::forDateRange(QDate(2026, 8, 29), QDate(2026, 8, 29));
    QCOMPARE(months, QList<QDate>{QDate(2026, 8, 1)});
}

void TestMonthKeys::rangeWithinOneMonth()
{
    const QList<QDate> months = MonthKeys::forDateRange(QDate(2026, 8, 3), QDate(2026, 8, 30));
    QCOMPARE(months, QList<QDate>{QDate(2026, 8, 1)});
}

void TestMonthKeys::rangeSpanningTwoMonths()
{
    // A week that straddles the Aug/Sep 2026 boundary.
    const QList<QDate> months = MonthKeys::forDateRange(QDate(2026, 8, 31), QDate(2026, 9, 6));
    QCOMPARE(months, (QList<QDate>{QDate(2026, 8, 1), QDate(2026, 9, 1)}));
}

void TestMonthKeys::rangeSpanningWholeMonthGrid()
{
    // The 42-day grid for August 2026 (Monday-start) runs Jul 27 .. Sep 6.
    const QList<QDate> months = MonthKeys::forDateRange(QDate(2026, 7, 27), QDate(2026, 9, 6));
    QCOMPARE(months, (QList<QDate>{QDate(2026, 7, 1), QDate(2026, 8, 1), QDate(2026, 9, 1)}));
}

void TestMonthKeys::rangeSpanningYearBoundary()
{
    const QList<QDate> months = MonthKeys::forDateRange(QDate(2026, 12, 28), QDate(2027, 1, 3));
    QCOMPARE(months, (QList<QDate>{QDate(2026, 12, 1), QDate(2027, 1, 1)}));
}

void TestMonthKeys::invalidOrReversedRangeIsEmpty()
{
    QVERIFY(MonthKeys::forDateRange(QDate(), QDate(2026, 8, 1)).isEmpty());
    QVERIFY(MonthKeys::forDateRange(QDate(2026, 8, 1), QDate()).isEmpty());
    QVERIFY(MonthKeys::forDateRange(QDate(2026, 9, 1), QDate(2026, 8, 1)).isEmpty());
}

QTEST_APPLESS_MAIN(TestMonthKeys)
#include "test_monthkeys.moc"
