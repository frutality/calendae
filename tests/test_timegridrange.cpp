#include "calendar/timegridrange.h"

#include <QTest>

class TestTimeGridRange : public QObject
{
    Q_OBJECT
private slots:
    void weekStartSnapsToMonday();
    void weekStartIsIdempotentOnTheStartDayItself();
    void weekStartShiftsWithFirstDayOfWeek();
    void datesForRangeReturnsRequestedCount();
    void datesForRangeIsConsecutive();
    void datesForRangeSingleDayIsJustStartDate();
};

void TestTimeGridRange::weekStartSnapsToMonday()
{
    // 2026-08-27 is a Thursday.
    QCOMPARE(TimeGridRange::weekStart(QDate(2026, 8, 27), Qt::Monday), QDate(2026, 8, 24));
}

void TestTimeGridRange::weekStartIsIdempotentOnTheStartDayItself()
{
    QCOMPARE(TimeGridRange::weekStart(QDate(2026, 8, 24), Qt::Monday), QDate(2026, 8, 24));
}

void TestTimeGridRange::weekStartShiftsWithFirstDayOfWeek()
{
    // 2026-08-27 is a Thursday; a Sunday-first week starts on 2026-08-23.
    QCOMPARE(TimeGridRange::weekStart(QDate(2026, 8, 27), Qt::Sunday), QDate(2026, 8, 23));
}

void TestTimeGridRange::datesForRangeReturnsRequestedCount()
{
    QCOMPARE(TimeGridRange::datesForRange(QDate(2026, 8, 24), 7).size(), 7);
    QCOMPARE(TimeGridRange::datesForRange(QDate(2026, 8, 24), 1).size(), 1);
}

void TestTimeGridRange::datesForRangeIsConsecutive()
{
    const QList<QDate> dates = TimeGridRange::datesForRange(QDate(2026, 8, 24), 7);
    QCOMPARE(dates.first(), QDate(2026, 8, 24));
    QCOMPARE(dates.last(), QDate(2026, 8, 30));
    for (int i = 1; i < dates.size(); ++i)
        QCOMPARE(dates.at(i - 1).addDays(1), dates.at(i));
}

void TestTimeGridRange::datesForRangeSingleDayIsJustStartDate()
{
    const QList<QDate> dates = TimeGridRange::datesForRange(QDate(2026, 8, 27), 1);
    QCOMPARE(dates.size(), 1);
    QCOMPARE(dates.first(), QDate(2026, 8, 27));
}

QTEST_APPLESS_MAIN(TestTimeGridRange)
#include "test_timegridrange.moc"
