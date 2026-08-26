#include "calendar/eventtimerange.h"
#include "calendar/monthgrid.h"

#include <QTest>
#include <QTimeZone>
#include <ctime>

class TestEventTimeRange : public QObject
{
    Q_OBJECT
private slots:
    void spansLocalMidnightBoundaries();
    void matchesMonthGridOutput();
    void handlesDstTransitionMonth();
    void springForwardConstructionStaysValid();
    void fallBackConstructionStaysValid();
};

void TestEventTimeRange::spansLocalMidnightBoundaries()
{
    const QList<QDate> dates = {QDate(2026, 8, 1), QDate(2026, 8, 2), QDate(2026, 8, 3)};
    const EventTimeRange::Range range = EventTimeRange::forDates(dates);

    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    const QDateTime expectedMin(QDate(2026, 8, 1), QTime(0, 0), localTimeZone);
    const QDateTime expectedMax(QDate(2026, 8, 4), QTime(0, 0), localTimeZone); // day after the last date

    QCOMPARE(QDateTime::fromString(range.timeMin, Qt::ISODate), expectedMin);
    QCOMPARE(QDateTime::fromString(range.timeMax, Qt::ISODate), expectedMax);

    // Always UTC ("Z"-suffixed) output.
    QVERIFY(range.timeMin.endsWith(QLatin1Char('Z')));
    QVERIFY(range.timeMax.endsWith(QLatin1Char('Z')));
}

void TestEventTimeRange::matchesMonthGridOutput()
{
    const QList<QDate> dates = MonthGrid::datesForGrid(QDate(2026, 8, 24), Qt::Monday);
    QCOMPARE(dates.size(), 42);

    const EventTimeRange::Range range = EventTimeRange::forDates(dates);
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    const QDateTime expectedMin(dates.first(), QTime(0, 0), localTimeZone);
    const QDateTime expectedMax(dates.last().addDays(1), QTime(0, 0), localTimeZone);

    QCOMPARE(QDateTime::fromString(range.timeMin, Qt::ISODate), expectedMin);
    QCOMPARE(QDateTime::fromString(range.timeMax, Qt::ISODate), expectedMax);
}

void TestEventTimeRange::handlesDstTransitionMonth()
{
    // Pin a DST-observing zone so the "spring forward" transition
    // (2026-03-08 in America/Los_Angeles) actually falls inside this
    // month's grid.
    qputenv("TZ", "America/Los_Angeles");
    tzset();

    const QList<QDate> dates = MonthGrid::datesForGrid(QDate(2026, 3, 15), Qt::Monday);
    QCOMPARE(dates.size(), 42);

    const EventTimeRange::Range range = EventTimeRange::forDates(dates);
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    const QDateTime expectedMin(dates.first(), QTime(0, 0), localTimeZone);
    const QDateTime expectedMax(dates.last().addDays(1), QTime(0, 0), localTimeZone);

    QVERIFY(expectedMin.isValid());
    QVERIFY(expectedMax.isValid());
    QCOMPARE(QDateTime::fromString(range.timeMin, Qt::ISODate), expectedMin);
    QCOMPARE(QDateTime::fromString(range.timeMax, Qt::ISODate), expectedMax);

    // The DST transition shifts the UTC offset mid-range but must never
    // change the number of calendar days the range covers.
    QCOMPARE(expectedMin.daysTo(expectedMax), static_cast<qint64>(dates.size()));
}

void TestEventTimeRange::springForwardConstructionStaysValid()
{
    qputenv("TZ", "America/Los_Angeles");
    tzset();

    // 2026-03-08 02:30 falls inside the "spring forward" gap (2:00-3:00 AM
    // does not exist that day in America/Los_Angeles). Regression guard
    // that this project's QTimeZone::LocalTime construction (used by both
    // EventDialog::buildRequest() and EventTimeRange::forDates()) never
    // produces a null/invalid QDateTime here, even though the wall-clock
    // instant is inherently ambiguous.
    const QDateTime dt(QDate(2026, 3, 8), QTime(2, 30), QTimeZone(QTimeZone::LocalTime));
    QVERIFY(dt.isValid());
}

void TestEventTimeRange::fallBackConstructionStaysValid()
{
    qputenv("TZ", "America/Los_Angeles");
    tzset();

    // 2026-11-01 01:30 occurs twice ("fall back" ambiguous hour) in
    // America/Los_Angeles. Same regression guard as above, for the other
    // direction of DST transition.
    const QDateTime dt(QDate(2026, 11, 1), QTime(1, 30), QTimeZone(QTimeZone::LocalTime));
    QVERIFY(dt.isValid());
}

QTEST_APPLESS_MAIN(TestEventTimeRange)
#include "test_eventtimerange.moc"
