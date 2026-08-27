#include "calendar/eventgrouping.h"

#include <QTest>
#include <QTimeZone>

class TestEventGrouping : public QObject
{
    Q_OBJECT
private slots:
    void timedEventLandsOnItsStartDateWithInstantsSet();
    void allDaySingleDayEventLandsOnlyOnStartDate();
    void allDayMultiDayEventRepeatsOnEveryCoveredDay();
    void missingSummaryFallsBackToNoTitle();
    void unknownCalendarFallsBackToGray();
    void spanIsCappedDefensively();
};

void TestEventGrouping::timedEventLandsOnItsStartDateWithInstantsSet()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    Event event;
    event.id = QStringLiteral("evt1");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Standup");
    event.allDay = false;
    event.startDate = QDate(2026, 8, 27);
    event.endDate = QDate(2026, 8, 27);
    event.startDateTime = QDateTime(QDate(2026, 8, 27), QTime(9, 0), localTimeZone);
    event.endDateTime = QDateTime(QDate(2026, 8, 27), QTime(9, 30), localTimeZone);

    Calendar calendar;
    calendar.id = QStringLiteral("cal1");
    calendar.color = QColor(Qt::blue);
    QHash<QString, Calendar> calendarsById{{calendar.id, calendar}};

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, calendarsById);

    QCOMPARE(grouped.size(), 1);
    const QList<MonthDayEventItem> items = grouped.value(QDate(2026, 8, 27));
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().title, QStringLiteral("Standup"));
    QCOMPARE(items.first().color, QColor(Qt::blue));
    QVERIFY(!items.first().allDay);
    QVERIFY(items.first().startInstant.isValid());
    QVERIFY(items.first().endInstant.isValid());
    QCOMPARE(items.first().startInstant, event.startDateTime);
    QCOMPARE(items.first().endInstant, event.endDateTime);
    QVERIFY(!items.first().timeLabel.isEmpty());
}

void TestEventGrouping::allDaySingleDayEventLandsOnlyOnStartDate()
{
    Event event;
    event.id = QStringLiteral("evt2");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Holiday");
    event.allDay = true;
    event.startDate = QDate(2026, 8, 27);
    event.endDate = QDate(2026, 8, 28); // exclusive, per Google semantics

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QCOMPARE(grouped.size(), 1);
    QVERIFY(grouped.contains(QDate(2026, 8, 27)));
    QVERIFY(!grouped.contains(QDate(2026, 8, 28)));
    QVERIFY(grouped.value(QDate(2026, 8, 27)).first().allDay);
    QVERIFY(!grouped.value(QDate(2026, 8, 27)).first().startInstant.isValid());
}

void TestEventGrouping::allDayMultiDayEventRepeatsOnEveryCoveredDay()
{
    Event event;
    event.id = QStringLiteral("evt3");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Conference");
    event.allDay = true;
    event.startDate = QDate(2026, 8, 27);
    event.endDate = QDate(2026, 8, 30); // exclusive -> covers 27, 28, 29

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QCOMPARE(grouped.size(), 3);
    QVERIFY(grouped.contains(QDate(2026, 8, 27)));
    QVERIFY(grouped.contains(QDate(2026, 8, 28)));
    QVERIFY(grouped.contains(QDate(2026, 8, 29)));
    QVERIFY(!grouped.contains(QDate(2026, 8, 30)));
}

void TestEventGrouping::missingSummaryFallsBackToNoTitle()
{
    Event event;
    event.id = QStringLiteral("evt4");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QString(); // empty — legitimate untitled event
    event.allDay = true;
    event.startDate = QDate(2026, 8, 27);
    event.endDate = QDate(2026, 8, 28);

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QVERIFY(!grouped.value(QDate(2026, 8, 27)).first().title.isEmpty());
    QVERIFY(grouped.value(QDate(2026, 8, 27)).first().title != event.summary);
}

void TestEventGrouping::unknownCalendarFallsBackToGray()
{
    Event event;
    event.id = QStringLiteral("evt5");
    event.calendarId = QStringLiteral("does-not-exist");
    event.summary = QStringLiteral("Orphaned");
    event.allDay = true;
    event.startDate = QDate(2026, 8, 27);
    event.endDate = QDate(2026, 8, 28);

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QCOMPARE(grouped.value(QDate(2026, 8, 27)).first().color, QColor(Qt::gray));
}

void TestEventGrouping::spanIsCappedDefensively()
{
    Event event;
    event.id = QStringLiteral("evt6");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Pathological");
    event.allDay = true;
    event.startDate = QDate(2026, 1, 1);
    event.endDate = QDate(2030, 1, 1); // far beyond the 90-day defensive cap

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QCOMPARE(grouped.size(), 90);
}

QTEST_APPLESS_MAIN(TestEventGrouping)
#include "test_eventgrouping.moc"
