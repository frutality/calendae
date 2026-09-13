#include "calendar/googlecalendarapi.h"

#include "fakenetworkaccessmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
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
    void buildsCreateEventBodyForTimedEventCrossingMidnight();
    void buildsCreateEventBodyForMultiDayAllDayEvent();
    void omitsEmptyDescriptionFromCreateEventBody();
    void buildsEventDetailUrl();
    void buildsUpdateEventBodyForAllDayEvent();
    void buildsUpdateEventBodyForTimedEvent();
    void updateEventBodyAlwaysIncludesDescriptionEvenWhenEmpty();
    void unchangedReminderModeOmitsRemindersFromBothBodies();
    void popupReminderModeEmitsExplicitOverride();
    void offReminderModeClearsPopupButKeepsPreservedOverrides();
    void classifiesTransientNetworkErrors();

    // Real request-sending slots, against a fake network + a real
    // AuthManager forced SignedIn via setSignedInForTesting() (never a real
    // restoreSession()/signIn(), which would touch the real OS keychain).
    void fetchCalendarListFailsWhenNotSignedIn();
    void fetchCalendarListParsesSuccessResponse();
    void fetchCalendarList401IsSessionExpired();
    void fetchCalendarListTransientFailureIsFlagged();
    void fetchCalendarListMalformedBodyFails();

    void setCalendarSelectedSendsPatchAndSucceeds();
    void setCalendarSelectedFailureRollsBackWithMessage();
    void setCalendarSelectedFailsWhenNotSignedIn();
    void setCalendarSelected401IsSessionExpired();
    void setCalendarSelectedEmptyBodyFailureUsesErrorString();

    void fetchEventsFailsWhenNotSignedIn();
    void fetchEventsFollowsPaginationAndAccumulates();
    void fetchEvents401IsSessionExpired();
    void fetchEventsTransientFailureIsFlagged();
    void fetchEventsMalformedBodyFails();
    void fetchEventsTooManyPagesFailsGracefully();

    void createEventSucceeds();
    void createEventForbiddenIncludesApiMessage();
    void createEventFailsWhenNotSignedIn();
    void createEvent401IsSessionExpired();
    void createEventEmptyBodyFailureUsesErrorString();
    void createEventGenericServerErrorIncludesApiMessage();

    void updateEventSucceeds();
    void updateEventNotFoundIncludesApiMessage();
    void updateEventFailsWhenNotSignedIn();
    void updateEvent401IsSessionExpired();
    void updateEventForbiddenIncludesApiMessage();
    void updateEventEmptyBodyFailureUsesErrorString();

    void deleteEventSucceeds();
    void deleteEventGoneIsTreatedAsSuccess();
    void deleteEventFailsWhenNotSignedIn();
    void deleteEvent401IsSessionExpired();
    void deleteEvent403Forbidden();
    void deleteEvent404NotFound();
    void deleteEventEmptyBodyFailureUsesErrorString();
    void deleteEventGenericServerError();
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

void TestGoogleCalendarApi::buildsCreateEventBodyForTimedEventCrossingMidnight()
{
    // A 23:00 -> 01:00 event: start and end fall on different calendar days.
    // Both instants must survive as UTC dateTime values with end > start.
    const QTimeZone localTimeZone(QTimeZone::LocalTime);

    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Late shift");
    request.allDay = false;
    request.startDateTime = QDateTime(QDate(2026, 8, 25), QTime(23, 0), localTimeZone);
    request.endDateTime = QDateTime(QDate(2026, 8, 26), QTime(1, 0), localTimeZone);

    const QJsonObject obj = QJsonDocument::fromJson(GoogleCalendarApi::buildCreateEventBody(request)).object();
    const QString startDateTime = obj.value(QStringLiteral("start")).toObject().value(QStringLiteral("dateTime")).toString();
    const QString endDateTime = obj.value(QStringLiteral("end")).toObject().value(QStringLiteral("dateTime")).toString();

    QVERIFY(startDateTime.endsWith(QLatin1Char('Z')));
    QVERIFY(endDateTime.endsWith(QLatin1Char('Z')));
    const QDateTime parsedStart = QDateTime::fromString(startDateTime, Qt::ISODate);
    const QDateTime parsedEnd = QDateTime::fromString(endDateTime, Qt::ISODate);
    QCOMPARE(parsedStart, request.startDateTime);
    QCOMPARE(parsedEnd, request.endDateTime);
    QVERIFY(parsedEnd > parsedStart);
}

void TestGoogleCalendarApi::buildsCreateEventBodyForMultiDayAllDayEvent()
{
    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Conference");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 28); // covers Aug 25-27 inclusive

    const QJsonObject obj = QJsonDocument::fromJson(GoogleCalendarApi::buildCreateEventBody(request)).object();
    const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
    const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
    QCOMPARE(start.value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-25"));
    QCOMPARE(end.value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-28"));
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

namespace {
NewEventRequest timedRequest()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    NewEventRequest request;
    request.calendarId = QStringLiteral("someone@example.com");
    request.summary = QStringLiteral("Standup");
    request.allDay = false;
    request.startDateTime = QDateTime(QDate(2026, 8, 25), QTime(9, 0), localTimeZone);
    request.endDateTime = QDateTime(QDate(2026, 8, 25), QTime(9, 30), localTimeZone);
    return request;
}
} // namespace

void TestGoogleCalendarApi::unchangedReminderModeOmitsRemindersFromBothBodies()
{
    NewEventRequest request = timedRequest();
    request.reminderMode = NewEventRequest::ReminderMode::Unchanged;

    QVERIFY(!QJsonDocument::fromJson(GoogleCalendarApi::buildCreateEventBody(request))
                 .object().contains(QStringLiteral("reminders")));
    QVERIFY(!QJsonDocument::fromJson(GoogleCalendarApi::buildUpdateEventBody(request))
                 .object().contains(QStringLiteral("reminders")));
}

void TestGoogleCalendarApi::popupReminderModeEmitsExplicitOverride()
{
    NewEventRequest request = timedRequest();
    request.reminderMode = NewEventRequest::ReminderMode::Popup;
    request.popupReminderMinutes = 120;

    for (const QByteArray &body : {GoogleCalendarApi::buildCreateEventBody(request),
                                    GoogleCalendarApi::buildUpdateEventBody(request)}) {
        const QJsonObject reminders = QJsonDocument::fromJson(body).object()
                                          .value(QStringLiteral("reminders")).toObject();
        QCOMPARE(reminders.value(QStringLiteral("useDefault")).toBool(true), false);
        const QJsonArray overrides = reminders.value(QStringLiteral("overrides")).toArray();
        QCOMPARE(overrides.size(), 1);
        QCOMPARE(overrides.first().toObject().value(QStringLiteral("method")).toString(), QStringLiteral("popup"));
        QCOMPARE(overrides.first().toObject().value(QStringLiteral("minutes")).toInt(), 120);
    }
}

void TestGoogleCalendarApi::offReminderModeClearsPopupButKeepsPreservedOverrides()
{
    NewEventRequest request = timedRequest();
    request.reminderMode = NewEventRequest::ReminderMode::Off;
    request.preservedReminderOverrides = {{QStringLiteral("email"), 1440}};

    const QJsonObject reminders = QJsonDocument::fromJson(GoogleCalendarApi::buildUpdateEventBody(request))
                                      .object().value(QStringLiteral("reminders")).toObject();
    QCOMPARE(reminders.value(QStringLiteral("useDefault")).toBool(true), false);
    const QJsonArray overrides = reminders.value(QStringLiteral("overrides")).toArray();
    QCOMPARE(overrides.size(), 1);
    QCOMPARE(overrides.first().toObject().value(QStringLiteral("method")).toString(), QStringLiteral("email"));
}

void TestGoogleCalendarApi::classifiesTransientNetworkErrors()
{
    // Connectivity-class errors with no HTTP status: transient.
    QVERIFY(GoogleCalendarApi::isTransientNetworkError(QNetworkReply::ConnectionRefusedError, 0));
    QVERIFY(GoogleCalendarApi::isTransientNetworkError(QNetworkReply::HostNotFoundError, 0));
    QVERIFY(GoogleCalendarApi::isTransientNetworkError(QNetworkReply::TimeoutError, 0));
    QVERIFY(GoogleCalendarApi::isTransientNetworkError(QNetworkReply::TemporaryNetworkFailureError, 0));
    QVERIFY(GoogleCalendarApi::isTransientNetworkError(QNetworkReply::ProxyConnectionRefusedError, 0));

    // The server answered — never transient, whatever the QNetworkReply code says.
    QVERIFY(!GoogleCalendarApi::isTransientNetworkError(QNetworkReply::ContentAccessDenied, 401));
    QVERIFY(!GoogleCalendarApi::isTransientNetworkError(QNetworkReply::UnknownContentError, 403));
    QVERIFY(!GoogleCalendarApi::isTransientNetworkError(QNetworkReply::InternalServerError, 500));
    QVERIFY(!GoogleCalendarApi::isTransientNetworkError(QNetworkReply::ConnectionRefusedError, 503));

    // Non-connectivity errors without a status: not transient.
    QVERIFY(!GoogleCalendarApi::isTransientNetworkError(QNetworkReply::NoError, 0));
    QVERIFY(!GoogleCalendarApi::isTransientNetworkError(QNetworkReply::ProtocolFailure, 0));
}

void TestGoogleCalendarApi::fetchCalendarListFailsWhenNotSignedIn()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net); // never signed in

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarListFetchFailed);

    api.fetchCalendarList();

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), QStringLiteral("You are not signed in."));
    QCOMPARE(failedSpy.first().at(1).toBool(), false);
    QCOMPARE(net.requests.count(), 0);
}

void TestGoogleCalendarApi::fetchCalendarListParsesSuccessResponse()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, R"({
            "items": [
                {"id": "primary@example.com", "summary": "Primary", "accessRole": "owner"}
            ]
        })"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy fetchedSpy(&api, &GoogleCalendarApi::calendarListFetched);

    api.fetchCalendarList();

    QVERIFY(fetchedSpy.wait());
    const auto calendars = fetchedSpy.first().at(0).value<QList<Calendar>>();
    QCOMPARE(calendars.size(), 1);
    QCOMPARE(calendars.first().id, QStringLiteral("primary@example.com"));
    QCOMPARE(net.requests.count(), 1);
    QCOMPARE(net.requests.first().operation, QNetworkAccessManager::GetOperation);
}

void TestGoogleCalendarApi::fetchCalendarList401IsSessionExpired()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{401, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarListFetchFailed);

    api.fetchCalendarList();

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(0).toString().contains(QStringLiteral("session may have expired")));
    QCOMPARE(failedSpy.first().at(1).toBool(), false);
}

void TestGoogleCalendarApi::fetchCalendarListTransientFailureIsFlagged()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarListFetchFailed);

    api.fetchCalendarList();

    QVERIFY(failedSpy.wait());
    QCOMPARE(failedSpy.first().at(1).toBool(), true);
}

void TestGoogleCalendarApi::setCalendarSelectedSendsPatchAndSucceeds()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarSelectedChangeFailed);

    api.setCalendarSelected(QStringLiteral("someone@example.com"), true);

    QTest::qWait(50); // let the fake reply's queued completion run
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(net.requests.count(), 1);
    QCOMPARE(net.requests.first().verb, QStringLiteral("PATCH"));
    QVERIFY(net.requests.first().url.toString().contains(QStringLiteral("someone%40example.com")));
    QCOMPARE(net.requests.first().body, QByteArray(R"({"selected":true})"));
}

void TestGoogleCalendarApi::setCalendarSelectedFailureRollsBackWithMessage()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{403, R"({"error":{"message":"Forbidden"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarSelectedChangeFailed);

    api.setCalendarSelected(QStringLiteral("cal1"), false);

    QVERIFY(failedSpy.wait());
    QCOMPARE(failedSpy.first().at(0).toString(), QStringLiteral("cal1"));
    QCOMPARE(failedSpy.first().at(1).toBool(), true); // rolls back to !selected
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("Forbidden")));
}

void TestGoogleCalendarApi::fetchEventsFailsWhenNotSignedIn()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventsFetchFailed);

    const quint64 requestId = api.fetchEvents(QStringLiteral("cal1"), QStringLiteral("2026-01-01T00:00:00Z"),
                                               QStringLiteral("2026-02-01T00:00:00Z"));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toULongLong(), requestId);
    QCOMPARE(net.requests.count(), 0);
}

void TestGoogleCalendarApi::fetchEventsFollowsPaginationAndAccumulates()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &request) {
        const QUrlQuery query(request.url);
        if (query.queryItemValue(QStringLiteral("pageToken")).isEmpty()) {
            return FakeNetworkReply::Response{200, R"({
                "items": [{"id": "e1", "summary": "First", "start": {"date": "2026-08-24"}, "end": {"date": "2026-08-25"}}],
                "nextPageToken": "page2"
            })"};
        }
        return FakeNetworkReply::Response{200, R"({
            "items": [{"id": "e2", "summary": "Second", "start": {"date": "2026-08-25"}, "end": {"date": "2026-08-26"}}]
        })"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy fetchedSpy(&api, &GoogleCalendarApi::eventsFetched);

    api.fetchEvents(QStringLiteral("cal1"), QStringLiteral("2026-08-01T00:00:00Z"),
                     QStringLiteral("2026-09-01T00:00:00Z"));

    QVERIFY(fetchedSpy.wait());
    const auto events = fetchedSpy.first().at(2).value<QList<Event>>();
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).id, QStringLiteral("e1"));
    QCOMPARE(events.at(1).id, QStringLiteral("e2"));
    QCOMPARE(net.requests.count(), 2);
}

void TestGoogleCalendarApi::fetchEvents401IsSessionExpired()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{401, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventsFetchFailed);

    api.fetchEvents(QStringLiteral("cal1"), QStringLiteral("2026-08-01T00:00:00Z"),
                     QStringLiteral("2026-09-01T00:00:00Z"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("session may have expired")));
    QCOMPARE(failedSpy.first().at(3).toBool(), false);
}

void TestGoogleCalendarApi::createEventSucceeds()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy createdSpy(&api, &GoogleCalendarApi::eventCreated);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("New event");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.createEvent(42, request);

    QVERIFY(createdSpy.wait());
    QCOMPARE(createdSpy.first().at(0).toULongLong(), quint64(42));
    QCOMPARE(createdSpy.first().at(1).toString(), QStringLiteral("cal1"));
    QCOMPARE(net.requests.first().operation, QNetworkAccessManager::PostOperation);
}

void TestGoogleCalendarApi::createEventForbiddenIncludesApiMessage()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{403, R"({"error":{"message":"No access"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventCreateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("New event");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.createEvent(1, request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("No access")));
}

void TestGoogleCalendarApi::updateEventSucceeds()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy updatedSpy(&api, &GoogleCalendarApi::eventUpdated);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("Updated");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.updateEvent(7, QStringLiteral("evt1"), request);

    QVERIFY(updatedSpy.wait());
    QCOMPARE(updatedSpy.first().at(2).toString(), QStringLiteral("evt1"));
    QCOMPARE(net.requests.first().verb, QStringLiteral("PATCH"));
}

void TestGoogleCalendarApi::updateEventNotFoundIncludesApiMessage()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{404, R"({"error":{"message":"Not Found"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventUpdateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("Updated");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.updateEvent(7, QStringLiteral("evt1"), request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("no longer exists")));
}

void TestGoogleCalendarApi::deleteEventSucceeds()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, QByteArray()};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy deletedSpy(&api, &GoogleCalendarApi::eventDeleted);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(deletedSpy.wait());
    QCOMPARE(net.requests.first().verb, QStringLiteral("DELETE"));
}

void TestGoogleCalendarApi::deleteEventGoneIsTreatedAsSuccess()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{410, QByteArray()};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy deletedSpy(&api, &GoogleCalendarApi::eventDeleted);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(deletedSpy.wait());
    QCOMPARE(failedSpy.count(), 0);
}

void TestGoogleCalendarApi::fetchCalendarListMalformedBodyFails()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, "not json at all"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarListFetchFailed);

    api.fetchCalendarList();

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(0).toString().contains(QStringLiteral("HTTP 200")));
}

void TestGoogleCalendarApi::setCalendarSelectedFailsWhenNotSignedIn()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net); // never signed in

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarSelectedChangeFailed);

    api.setCalendarSelected(QStringLiteral("cal1"), true);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), QStringLiteral("cal1"));
    QCOMPARE(failedSpy.first().at(1).toBool(), false); // rolls back to !selected
    QCOMPARE(failedSpy.first().at(2).toString(), QStringLiteral("You are not signed in."));
    QCOMPARE(net.requests.count(), 0);
}

void TestGoogleCalendarApi::setCalendarSelected401IsSessionExpired()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{401, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarSelectedChangeFailed);

    api.setCalendarSelected(QStringLiteral("cal1"), true);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("session may have expired")));
}

void TestGoogleCalendarApi::setCalendarSelectedEmptyBodyFailureUsesErrorString()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarSelectedChangeFailed);

    api.setCalendarSelected(QStringLiteral("cal1"), true);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("Connection refused")));
}

void TestGoogleCalendarApi::fetchEventsTransientFailureIsFlagged()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventsFetchFailed);

    api.fetchEvents(QStringLiteral("cal1"), QStringLiteral("2026-08-01T00:00:00Z"),
                     QStringLiteral("2026-09-01T00:00:00Z"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("Connection refused")));
    QCOMPARE(failedSpy.first().at(3).toBool(), true);
}

void TestGoogleCalendarApi::fetchEventsMalformedBodyFails()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, "not json at all"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventsFetchFailed);

    api.fetchEvents(QStringLiteral("cal1"), QStringLiteral("2026-08-01T00:00:00Z"),
                     QStringLiteral("2026-09-01T00:00:00Z"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("HTTP 200")));
    QCOMPARE(failedSpy.first().at(3).toBool(), false);
}

void TestGoogleCalendarApi::fetchEventsTooManyPagesFailsGracefully()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    // Always hands back a nextPageToken, so pagination never terminates on
    // its own and must be stopped by the page-count guard.
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, R"({"items": [], "nextPageToken": "next"})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventsFetchFailed);

    api.fetchEvents(QStringLiteral("cal1"), QStringLiteral("2026-08-01T00:00:00Z"),
                     QStringLiteral("2026-09-01T00:00:00Z"));

    QVERIFY(failedSpy.wait(10000));
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("too many result pages")));
}

void TestGoogleCalendarApi::createEventFailsWhenNotSignedIn()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net); // never signed in

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventCreateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("New event");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.createEvent(1, request);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(2).toString(), QStringLiteral("You are not signed in."));
    QCOMPARE(net.requests.count(), 0);
}

void TestGoogleCalendarApi::createEvent401IsSessionExpired()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{401, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventCreateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("New event");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.createEvent(1, request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("session may have expired")));
}

void TestGoogleCalendarApi::createEventEmptyBodyFailureUsesErrorString()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventCreateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("New event");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.createEvent(1, request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("Connection refused")));
}

void TestGoogleCalendarApi::createEventGenericServerErrorIncludesApiMessage()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{500, R"({"error":{"message":"Internal error"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventCreateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("New event");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.createEvent(1, request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(2).toString().contains(QStringLiteral("Internal error")));
}

void TestGoogleCalendarApi::updateEventFailsWhenNotSignedIn()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net); // never signed in

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventUpdateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("Updated");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.updateEvent(7, QStringLiteral("evt1"), request);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(3).toString(), QStringLiteral("You are not signed in."));
    QCOMPARE(net.requests.count(), 0);
}

void TestGoogleCalendarApi::updateEvent401IsSessionExpired()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{401, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventUpdateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("Updated");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.updateEvent(7, QStringLiteral("evt1"), request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("session may have expired")));
}

void TestGoogleCalendarApi::updateEventForbiddenIncludesApiMessage()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{403, R"({"error":{"message":"No access"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventUpdateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("Updated");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.updateEvent(7, QStringLiteral("evt1"), request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("No access")));
}

void TestGoogleCalendarApi::updateEventEmptyBodyFailureUsesErrorString()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventUpdateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("cal1");
    request.summary = QStringLiteral("Updated");
    request.allDay = true;
    request.startDate = QDate(2026, 8, 25);
    request.endDateExclusive = QDate(2026, 8, 26);

    api.updateEvent(7, QStringLiteral("evt1"), request);

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("Connection refused")));
}

void TestGoogleCalendarApi::deleteEventFailsWhenNotSignedIn()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net); // never signed in

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(3).toString(), QStringLiteral("You are not signed in."));
    QCOMPARE(net.requests.count(), 0);
}

void TestGoogleCalendarApi::deleteEvent401IsSessionExpired()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{401, "{}"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("session may have expired")));
}

void TestGoogleCalendarApi::deleteEvent403Forbidden()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{403, R"({"error":{"message":"No access"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("No access")));
}

void TestGoogleCalendarApi::deleteEvent404NotFound()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{404, R"({"error":{"message":"Not Found"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("no longer exists")));
}

void TestGoogleCalendarApi::deleteEventEmptyBodyFailureUsesErrorString()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("Connection refused")));
}

void TestGoogleCalendarApi::deleteEventGenericServerError()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    auth.setSignedInForTesting(QStringLiteral("test-access-token"));

    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{500, R"({"error":{"message":"Internal error"}})"};
    };

    GoogleCalendarApi api(&auth);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::eventDeleteFailed);

    api.deleteEvent(3, QStringLiteral("cal1"), QStringLiteral("evt1"));

    QVERIFY(failedSpy.wait());
    QVERIFY(failedSpy.first().at(3).toString().contains(QStringLiteral("Internal error")));
}

QTEST_GUILESS_MAIN(TestGoogleCalendarApi)
#include "test_googlecalendarapi.moc"
