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
    void timedEventEndingAtMidnightStaysOnStartDay();
    void timedEventCrossingMidnightRepeatsOnBothDays();
    void missingSummaryFallsBackToNoTitle();
    void unknownCalendarFallsBackToGray();
    void spanIsCappedDefensively();
    void containsEventOnAnyDateFindsEventOnVisibleDay();
    void containsEventOnAnyDateIgnoresEventOffVisibleRange();
    void containsEventOnAnyDateRejectsUnknownEventCalendarOrEmptyId();
};

namespace {
QHash<QString, QHash<QDate, QList<MonthDayEventItem>>> groupedWith(const QString &calendarId,
                                                                   const QString &eventId,
                                                                   const QList<QDate> &onDates)
{
    QHash<QDate, QList<MonthDayEventItem>> byDate;
    for (const QDate &date : onDates) {
        MonthDayEventItem item;
        item.eventId = eventId;
        item.calendarId = calendarId;
        byDate[date].append(item);
    }
    return {{calendarId, byDate}};
}
} // namespace

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

void TestEventGrouping::timedEventEndingAtMidnightStaysOnStartDay()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    Event event;
    event.id = QStringLiteral("evt-midnight");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Evening meeting");
    event.allDay = false;
    event.startDate = QDate(2026, 9, 1);
    event.endDate = QDate(2026, 9, 2); // local date of a 00:00 end instant
    event.startDateTime = QDateTime(QDate(2026, 9, 1), QTime(22, 0), localTimeZone);
    event.endDateTime = QDateTime(QDate(2026, 9, 2), QTime(0, 0), localTimeZone);

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QCOMPARE(grouped.size(), 1);
    QVERIFY(grouped.contains(QDate(2026, 9, 1)));
    QVERIFY(!grouped.contains(QDate(2026, 9, 2)));
}

void TestEventGrouping::timedEventCrossingMidnightRepeatsOnBothDays()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    Event event;
    event.id = QStringLiteral("evt-overnight");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Night shift");
    event.allDay = false;
    event.startDate = QDate(2026, 9, 1);
    event.endDate = QDate(2026, 9, 2);
    event.startDateTime = QDateTime(QDate(2026, 9, 1), QTime(23, 0), localTimeZone);
    event.endDateTime = QDateTime(QDate(2026, 9, 2), QTime(0, 30), localTimeZone);

    const QHash<QDate, QList<MonthDayEventItem>> grouped = EventGrouping::groupByDate({event}, {});

    QCOMPARE(grouped.size(), 2);
    QVERIFY(grouped.contains(QDate(2026, 9, 1)));
    QVERIFY(grouped.contains(QDate(2026, 9, 2)));
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

void TestEventGrouping::containsEventOnAnyDateFindsEventOnVisibleDay()
{
    const auto grouped = groupedWith(QStringLiteral("cal1"), QStringLiteral("evtA"),
                                     {QDate(2026, 9, 3)});
    const QList<QDate> visibleWeek = {QDate(2026, 8, 31), QDate(2026, 9, 1), QDate(2026, 9, 2),
                                      QDate(2026, 9, 3), QDate(2026, 9, 4), QDate(2026, 9, 5),
                                      QDate(2026, 9, 6)};

    QVERIFY(EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("cal1"),
                                                 QStringLiteral("evtA"), visibleWeek));
}

void TestEventGrouping::containsEventOnAnyDateIgnoresEventOffVisibleRange()
{
    // The event lives in the loaded month but on a day the week view no
    // longer shows (a background refresh moved it). Selection must drop.
    const auto grouped = groupedWith(QStringLiteral("cal1"), QStringLiteral("evtA"),
                                     {QDate(2026, 9, 20)});
    const QList<QDate> visibleWeek = {QDate(2026, 8, 31), QDate(2026, 9, 1), QDate(2026, 9, 2),
                                      QDate(2026, 9, 3), QDate(2026, 9, 4), QDate(2026, 9, 5),
                                      QDate(2026, 9, 6)};

    QVERIFY(!EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("cal1"),
                                                  QStringLiteral("evtA"), visibleWeek));

    // Same event, day view now landing on its actual day -> visible again.
    QVERIFY(EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("cal1"),
                                                 QStringLiteral("evtA"), {QDate(2026, 9, 20)}));
}

void TestEventGrouping::containsEventOnAnyDateRejectsUnknownEventCalendarOrEmptyId()
{
    const auto grouped = groupedWith(QStringLiteral("cal1"), QStringLiteral("evtA"),
                                     {QDate(2026, 9, 3)});
    const QList<QDate> dates = {QDate(2026, 9, 3)};

    QVERIFY(!EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("cal1"),
                                                  QStringLiteral("evtGONE"), dates));
    QVERIFY(!EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("other-cal"),
                                                  QStringLiteral("evtA"), dates));
    QVERIFY(!EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("cal1"),
                                                  QString(), dates));
    QVERIFY(!EventGrouping::containsEventOnAnyDate(grouped, QStringLiteral("cal1"),
                                                  QStringLiteral("evtA"), {}));
}

QTEST_APPLESS_MAIN(TestEventGrouping)
#include "test_eventgrouping.moc"
