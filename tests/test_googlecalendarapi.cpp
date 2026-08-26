#include "calendar/googlecalendarapi.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>
#include <QUrlQuery>

class TestGoogleCalendarApi : public QObject
{
    Q_OBJECT
private slots:
    void buildsSelectedPatchBody();
    void extractsApiErrorMessage();
    void fallsBackToHttpStatusWhenNoMessage();
    void buildsEventsListUrl();
    void buildsCreateEventBodyForAllDayEvent();
    void buildsCreateEventBodyForTimedEvent();
    void omitsEmptyDescriptionFromCreateEventBody();
    void buildsEventDetailUrl();
    void buildsUpdateEventBodyForAllDayEvent();
    void buildsUpdateEventBodyForTimedEvent();
    void updateEventBodyAlwaysIncludesDescriptionEvenWhenEmpty();
};

void TestGoogleCalendarApi::buildsSelectedPatchBody()
{
    for (bool selected : {true, false}) {
        const QByteArray body = GoogleCalendarApi::buildSelectedPatchBody(selected);
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.size(), 1);
        QCOMPARE(obj.value(QStringLiteral("selected")).toBool(), selected);
    }
}

void TestGoogleCalendarApi::extractsApiErrorMessage()
{
    const QByteArray body = R"({
        "error": {
            "code": 401,
            "message": "Invalid Credentials",
            "errors": [{"domain": "global", "reason": "authError", "message": "Invalid Credentials"}]
        }
    })";

    QCOMPARE(GoogleCalendarApi::extractApiErrorMessage(body, 401), QStringLiteral("Invalid Credentials"));
}

void TestGoogleCalendarApi::fallsBackToHttpStatusWhenNoMessage()
{
    QCOMPARE(GoogleCalendarApi::extractApiErrorMessage(QByteArray(), 503), QStringLiteral("HTTP 503"));
    QCOMPARE(GoogleCalendarApi::extractApiErrorMessage("not json", 500), QStringLiteral("HTTP 500"));
}

void TestGoogleCalendarApi::buildsEventsListUrl()
{
    const QUrl url = GoogleCalendarApi::buildEventsListUrl(
        QStringLiteral("someone@example.com"),
        QStringLiteral("2026-08-01T00:00:00Z"),
        QStringLiteral("2026-09-01T00:00:00Z"));

    // QUrl::path() returns the decoded path by default; check the encoded
    // form separately to confirm the '@' is actually percent-encoded
    // on the wire.
    QVERIFY(url.path().endsWith(QStringLiteral("/calendars/someone@example.com/events")));
    QVERIFY(url.path(QUrl::FullyEncoded).endsWith(QStringLiteral("/calendars/someone%40example.com/events")));

    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("timeMin")), QStringLiteral("2026-08-01T00:00:00Z"));
    QCOMPARE(query.queryItemValue(QStringLiteral("timeMax")), QStringLiteral("2026-09-01T00:00:00Z"));
    QCOMPARE(query.queryItemValue(QStringLiteral("singleEvents")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("orderBy")), QStringLiteral("startTime"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("pageToken")));

    const QUrl pagedUrl = GoogleCalendarApi::buildEventsListUrl(
        QStringLiteral("someone@example.com"),
        QStringLiteral("2026-08-01T00:00:00Z"),
        QStringLiteral("2026-09-01T00:00:00Z"),
        QStringLiteral("token123"));
    const QUrlQuery pagedQuery(pagedUrl);
    QCOMPARE(pagedQuery.queryItemValue(QStringLiteral("pageToken")), QStringLiteral("token123"));
}

void TestGoogleCalendarApi::buildsCreateEventBodyForAllDayEvent()
{
    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Company Holiday");
    request.description = QStringLiteral("Office closed");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    const QByteArray body = GoogleCalendarApi::buildCreateEventBody(request);
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();

    QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("Company Holiday"));
    QCOMPARE(obj.value(QStringLiteral("description")).toString(), QStringLiteral("Office closed"));

    const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
    const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
    QCOMPARE(start.value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-25"));
    QCOMPARE(end.value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-26"));
    QVERIFY(!start.contains(QStringLiteral("dateTime")));
    QVERIFY(!end.contains(QStringLiteral("dateTime")));
}

void TestGoogleCalendarApi::buildsCreateEventBodyForTimedEvent()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);

    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Standup");
    request.allDay = false;
    request.startDateTime = QDateTime(QDate(2026, 8, 25), QTime(9, 0), localTimeZone);
    request.endDateTime = QDateTime(QDate(2026, 8, 25), QTime(9, 30), localTimeZone);

    const QByteArray body = GoogleCalendarApi::buildCreateEventBody(request);
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();

    const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
    const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
    QVERIFY(!start.contains(QStringLiteral("date")));
    QVERIFY(!end.contains(QStringLiteral("date")));

    const QString startDateTime = start.value(QStringLiteral("dateTime")).toString();
    const QString endDateTime = end.value(QStringLiteral("dateTime")).toString();
    QVERIFY(startDateTime.endsWith(QLatin1Char('Z')));
    QVERIFY(endDateTime.endsWith(QLatin1Char('Z')));
    QCOMPARE(QDateTime::fromString(startDateTime, Qt::ISODate), request.startDateTime);
    QCOMPARE(QDateTime::fromString(endDateTime, Qt::ISODate), request.endDateTime);
}

void TestGoogleCalendarApi::omitsEmptyDescriptionFromCreateEventBody()
{
    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("No description");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    const QByteArray body = GoogleCalendarApi::buildCreateEventBody(request);
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    QVERIFY(!obj.contains(QStringLiteral("description")));
}

void TestGoogleCalendarApi::buildsEventDetailUrl()
{
    const QUrl url = GoogleCalendarApi::buildEventDetailUrl(
        QStringLiteral("someone@example.com"), QStringLiteral("abc/id"));

    QVERIFY(url.path().endsWith(QStringLiteral("/calendars/someone@example.com/events/abc/id")));
    const QString encodedPath = url.path(QUrl::FullyEncoded);
    QVERIFY(encodedPath.endsWith(QStringLiteral("/calendars/someone%40example.com/events/abc%2Fid")));
}

void TestGoogleCalendarApi::buildsUpdateEventBodyForAllDayEvent()
{
    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Company Holiday");
    request.description = QStringLiteral("Office closed");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    const QByteArray body = GoogleCalendarApi::buildUpdateEventBody(request);
    const QJsonObject obj = QJsonDocument::fromJson(body).object();

    QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("Company Holiday"));
    QCOMPARE(obj.value(QStringLiteral("description")).toString(), QStringLiteral("Office closed"));

    const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
    const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
    QCOMPARE(start.value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-25"));
    QCOMPARE(end.value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-26"));
    QVERIFY(!start.contains(QStringLiteral("dateTime")));
}

void TestGoogleCalendarApi::buildsUpdateEventBodyForTimedEvent()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);

    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Standup");
    request.allDay = false;
    request.startDateTime = QDateTime(QDate(2026, 8, 25), QTime(9, 0), localTimeZone);
    request.endDateTime = QDateTime(QDate(2026, 8, 25), QTime(9, 30), localTimeZone);

    const QByteArray body = GoogleCalendarApi::buildUpdateEventBody(request);
    const QJsonObject obj = QJsonDocument::fromJson(body).object();

    const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
    const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
    QVERIFY(!start.contains(QStringLiteral("date")));

    const QString startDateTime = start.value(QStringLiteral("dateTime")).toString();
    const QString endDateTime = end.value(QStringLiteral("dateTime")).toString();
    QCOMPARE(QDateTime::fromString(startDateTime, Qt::ISODate), request.startDateTime);
    QCOMPARE(QDateTime::fromString(endDateTime, Qt::ISODate), request.endDateTime);
}

void TestGoogleCalendarApi::updateEventBodyAlwaysIncludesDescriptionEvenWhenEmpty()
{
    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Cleared description");
    request.description.clear();
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    const QByteArray body = GoogleCalendarApi::buildUpdateEventBody(request);
    const QJsonObject obj = QJsonDocument::fromJson(body).object();

    // Contrast with omitsEmptyDescriptionFromCreateEventBody: under PATCH's
    // partial-update semantics, omitting "description" would mean "leave
    // unchanged," not "clear it," so update must always send the key.
    QVERIFY(obj.contains(QStringLiteral("description")));
    QCOMPARE(obj.value(QStringLiteral("description")).toString(), QString());
}

QTEST_APPLESS_MAIN(TestGoogleCalendarApi)
#include "test_googlecalendarapi.moc"
