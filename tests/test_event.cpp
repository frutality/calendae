#include "calendar/event.h"

#include <QTest>
#include <ctime>

class TestEvent : public QObject
{
    Q_OBJECT
private slots:
    void parsesAllDayEvent();
    void parsesTimedEventWithOffset();
    void parsesTimedEventCrossDayRollover();
    void missingSummaryIsKeptEmpty();
    void parsesDescriptionAndRecurringEventIdWhenPresent();
    void descriptionAndRecurringEventIdDefaultToEmptyWhenAbsent();
    void parsesReminderOverridesKeepingEveryMethod();
    void useDefaultRemindersYieldNoOverrides();
    void absentRemindersFieldDefaultsToUseDefaultTrue();
    void skipsItemsMissingIdOrDateFields();
    void extractsNextPageToken();
    void rejectsMalformedJson();
};

void TestEvent::parsesAllDayEvent()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "abc123",
                "summary": "Company Holiday",
                "start": {"date": "2026-08-24"},
                "end": {"date": "2026-08-25"}
            }
        ]
    })";

    QString error;
    const auto events = Event::listFromJson(json, QStringLiteral("cal1"), nullptr, &error);
    QVERIFY(events.has_value());
    QVERIFY(error.isEmpty());
    QCOMPARE(events->size(), 1);

    const Event &event = events->first();
    QCOMPARE(event.id, QStringLiteral("abc123"));
    QCOMPARE(event.calendarId, QStringLiteral("cal1"));
    QCOMPARE(event.summary, QStringLiteral("Company Holiday"));
    QVERIFY(event.allDay);
    QCOMPARE(event.startDate, QDate(2026, 8, 24));
    QCOMPARE(event.endDate, QDate(2026, 8, 25));
}

void TestEvent::parsesTimedEventWithOffset()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "timed1",
                "summary": "Standup",
                "start": {"dateTime": "2026-08-24T09:00:00-07:00", "timeZone": "America/Los_Angeles"},
                "end": {"dateTime": "2026-08-24T09:30:00-07:00", "timeZone": "America/Los_Angeles"}
            }
        ]
    })";

    QString error;
    const auto events = Event::listFromJson(json, QStringLiteral("cal1"), nullptr, &error);
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);

    const Event &event = events->first();
    QVERIFY(!event.allDay);
    QCOMPARE(event.startDateTime, QDateTime::fromString(QStringLiteral("2026-08-24T09:00:00-07:00"), Qt::ISODate));
    QCOMPARE(event.endDateTime, QDateTime::fromString(QStringLiteral("2026-08-24T09:30:00-07:00"), Qt::ISODate));
}

void TestEvent::parsesTimedEventCrossDayRollover()
{
    // Pin the system timezone so .toLocalTime() is deterministic regardless
    // of the machine running the test. Linux/macOS only (tzset()); Windows
    // would need different env handling, not exercised here.
    qputenv("TZ", "America/Los_Angeles");
    tzset();

    // 23:30 UTC on Aug 24 is still Aug 24 in UTC, but 16:30 in
    // America/Los_Angeles (UTC-7 in August) -- same day here, so instead
    // pick a time that actually rolls over: 02:00 UTC on Aug 25 is 19:00
    // Aug 24 in America/Los_Angeles.
    const QByteArray json = R"({
        "items": [
            {
                "id": "rollover1",
                "start": {"dateTime": "2026-08-25T02:00:00Z"},
                "end": {"dateTime": "2026-08-25T03:00:00Z"}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);
    QCOMPARE(events->first().startDate, QDate(2026, 8, 24));
}

void TestEvent::missingSummaryIsKeptEmpty()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "untitled1",
                "start": {"date": "2026-08-24"},
                "end": {"date": "2026-08-25"}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);
    QVERIFY(events->first().summary.isEmpty());
}

void TestEvent::parsesDescriptionAndRecurringEventIdWhenPresent()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "abc123_20260825T090000Z",
                "summary": "Standup",
                "description": "Daily sync",
                "recurringEventId": "abc123",
                "start": {"date": "2026-08-24"},
                "end": {"date": "2026-08-25"}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);
    QCOMPARE(events->first().description, QStringLiteral("Daily sync"));
    QCOMPARE(events->first().recurringEventId, QStringLiteral("abc123"));
}

void TestEvent::descriptionAndRecurringEventIdDefaultToEmptyWhenAbsent()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "abc123",
                "summary": "One-off",
                "start": {"date": "2026-08-24"},
                "end": {"date": "2026-08-25"}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);
    QVERIFY(events->first().description.isEmpty());
    QVERIFY(events->first().recurringEventId.isEmpty());
}

void TestEvent::parsesReminderOverridesKeepingEveryMethod()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "e1",
                "summary": "Review",
                "start": {"dateTime": "2026-08-28T14:00:00Z"},
                "end": {"dateTime": "2026-08-28T15:00:00Z"},
                "reminders": {
                    "useDefault": false,
                    "overrides": [
                        {"method": "popup", "minutes": 120},
                        {"method": "email", "minutes": 1440}
                    ]
                }
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);

    const Event &event = events->first();
    QVERIFY(!event.remindersUseDefault);
    QCOMPARE(event.reminderOverrides.size(), 2);
    QCOMPARE(event.reminderOverrides.at(0).method, QStringLiteral("popup"));
    QCOMPARE(event.reminderOverrides.at(0).minutes, 120);
    QCOMPARE(event.reminderOverrides.at(1).method, QStringLiteral("email"));
    QCOMPARE(event.reminderOverrides.at(1).minutes, 1440);
}

void TestEvent::useDefaultRemindersYieldNoOverrides()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "e1",
                "summary": "Review",
                "start": {"dateTime": "2026-08-28T14:00:00Z"},
                "end": {"dateTime": "2026-08-28T15:00:00Z"},
                "reminders": {"useDefault": true}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QVERIFY(events->first().remindersUseDefault);
    QVERIFY(events->first().reminderOverrides.isEmpty());
}

void TestEvent::absentRemindersFieldDefaultsToUseDefaultTrue()
{
    const QByteArray json = R"({
        "items": [
            {
                "id": "e1",
                "summary": "Review",
                "start": {"dateTime": "2026-08-28T14:00:00Z"},
                "end": {"dateTime": "2026-08-28T15:00:00Z"}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QVERIFY(events->first().remindersUseDefault);
    QVERIFY(events->first().reminderOverrides.isEmpty());
}

void TestEvent::skipsItemsMissingIdOrDateFields()
{
    const QByteArray json = R"({
        "items": [
            {"summary": "No id", "start": {"date": "2026-08-24"}, "end": {"date": "2026-08-25"}},
            {"id": "no-dates", "summary": "No dates"},
            {
                "id": "valid1",
                "summary": "Valid",
                "start": {"date": "2026-08-24"},
                "end": {"date": "2026-08-25"}
            }
        ]
    })";

    const auto events = Event::listFromJson(json, QStringLiteral("cal1"));
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), 1);
    QCOMPARE(events->first().id, QStringLiteral("valid1"));
}

void TestEvent::extractsNextPageToken()
{
    const QByteArray jsonWithToken = R"({"items": [], "nextPageToken": "page2token"})";
    QString token;
    QVERIFY(Event::listFromJson(jsonWithToken, QStringLiteral("cal1"), &token).has_value());
    QCOMPARE(token, QStringLiteral("page2token"));

    const QByteArray jsonWithoutToken = R"({"items": []})";
    token = QStringLiteral("stale");
    QVERIFY(Event::listFromJson(jsonWithoutToken, QStringLiteral("cal1"), &token).has_value());
    QVERIFY(token.isEmpty());
}

void TestEvent::rejectsMalformedJson()
{
    QString error;
    QVERIFY(!Event::listFromJson("not json", QStringLiteral("cal1"), nullptr, &error).has_value());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!Event::listFromJson(R"({"kind":"calendar#events"})", QStringLiteral("cal1"), nullptr, &error).has_value());
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TestEvent)
#include "test_event.moc"
