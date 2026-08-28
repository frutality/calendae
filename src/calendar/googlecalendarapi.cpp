#include "googlecalendarapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
const QUrl kCalendarListEndpoint(QStringLiteral("https://www.googleapis.com/calendar/v3/users/me/calendarList"));

QString calendarListEntryUrl(const QString &calendarId)
{
    return QStringLiteral("https://www.googleapis.com/calendar/v3/users/me/calendarList/%1")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(calendarId)));
}

QString calendarEventsCollectionUrl(const QString &calendarId)
{
    return QStringLiteral("https://www.googleapis.com/calendar/v3/calendars/%1/events")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(calendarId)));
}

QString calendarEventEntryUrl(const QString &calendarId, const QString &eventId)
{
    return QStringLiteral("https://www.googleapis.com/calendar/v3/calendars/%1/events/%2")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(calendarId)),
             QString::fromUtf8(QUrl::toPercentEncoding(eventId)));
}
// Without this, a server that accepts the connection but never responds
// (black-holed by a firewall/proxy, hung process) leaves a request pending
// forever with no error ever surfaced to the user.
constexpr int kNetworkTimeoutMs = 20000;

// Serializes NewEventRequest's reminder fields into the shared request body,
// identically for create (POST) and update (PATCH): a Unchanged mode leaves
// the body alone (Google keeps the calendar default / existing reminders),
// otherwise "reminders" is written with useDefault:false and an explicit
// overrides list — preserved non-popup entries plus, for Popup mode, the
// one popup entry the dialog manages.
void insertRemindersIfSet(QJsonObject &obj, const NewEventRequest &request)
{
    if (request.reminderMode == NewEventRequest::ReminderMode::Unchanged)
        return;

    QJsonArray overrides;
    for (const EventReminder &reminder : request.preservedReminderOverrides) {
        overrides.append(QJsonObject{
            {QStringLiteral("method"), reminder.method},
            {QStringLiteral("minutes"), reminder.minutes},
        });
    }
    if (request.reminderMode == NewEventRequest::ReminderMode::Popup) {
        overrides.append(QJsonObject{
            {QStringLiteral("method"), QStringLiteral("popup")},
            {QStringLiteral("minutes"), request.popupReminderMinutes},
        });
    }

    obj.insert(QStringLiteral("reminders"), QJsonObject{
        {QStringLiteral("useDefault"), false},
        {QStringLiteral("overrides"), overrides},
    });
}
} // namespace

GoogleCalendarApi::GoogleCalendarApi(AuthManager *authManager, QObject *parent)
    : QObject(parent)
    , m_authManager(authManager)
    , m_network(new QNetworkAccessManager(this))
{
}

QByteArray GoogleCalendarApi::buildSelectedPatchBody(bool selected)
{
    const QJsonObject obj{
        {QStringLiteral("selected"), selected},
    };
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QString GoogleCalendarApi::extractApiErrorMessage(const QByteArray &body, int httpStatusCode)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject error = doc.object().value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            return message;
    }
    return QStringLiteral("HTTP %1").arg(httpStatusCode);
}

QUrl GoogleCalendarApi::buildEventsListUrl(const QString &calendarId, const QString &timeMinRfc3339,
                                            const QString &timeMaxRfc3339, const QString &pageToken)
{
    QUrl url(calendarEventsCollectionUrl(calendarId));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("timeMin"), timeMinRfc3339);
    query.addQueryItem(QStringLiteral("timeMax"), timeMaxRfc3339);
    query.addQueryItem(QStringLiteral("singleEvents"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("orderBy"), QStringLiteral("startTime"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QStringLiteral("pageToken"), pageToken);
    url.setQuery(query);
    return url;
}

QByteArray GoogleCalendarApi::buildCreateEventBody(const NewEventRequest &request)
{
    QJsonObject obj{
        {QStringLiteral("summary"), request.summary},
    };
    if (!request.description.isEmpty())
        obj.insert(QStringLiteral("description"), request.description);

    QJsonObject start;
    QJsonObject end;
    if (request.allDay) {
        start.insert(QStringLiteral("date"), request.startDate.toString(Qt::ISODate));
        end.insert(QStringLiteral("date"), request.endDateExclusive.toString(Qt::ISODate));
    } else {
        start.insert(QStringLiteral("dateTime"), request.startDateTime.toUTC().toString(Qt::ISODate));
        end.insert(QStringLiteral("dateTime"), request.endDateTime.toUTC().toString(Qt::ISODate));
    }
    obj.insert(QStringLiteral("start"), start);
    obj.insert(QStringLiteral("end"), end);

    insertRemindersIfSet(obj, request);

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QUrl GoogleCalendarApi::buildEventDetailUrl(const QString &calendarId, const QString &eventId)
{
    return QUrl(calendarEventEntryUrl(calendarId, eventId));
}

QByteArray GoogleCalendarApi::buildUpdateEventBody(const NewEventRequest &request)
{
    // Unlike buildCreateEventBody, description is always included (even as
    // ""): under PATCH's partial-update semantics, an omitted field means
    // "leave unchanged," not "clear it" — a brand-new event has nothing to
    // clear, but an edited one might.
    QJsonObject obj{
        {QStringLiteral("summary"), request.summary},
        {QStringLiteral("description"), request.description},
    };

    QJsonObject start;
    QJsonObject end;
    if (request.allDay) {
        start.insert(QStringLiteral("date"), request.startDate.toString(Qt::ISODate));
        end.insert(QStringLiteral("date"), request.endDateExclusive.toString(Qt::ISODate));
    } else {
        start.insert(QStringLiteral("dateTime"), request.startDateTime.toUTC().toString(Qt::ISODate));
        end.insert(QStringLiteral("dateTime"), request.endDateTime.toUTC().toString(Qt::ISODate));
    }
    obj.insert(QStringLiteral("start"), start);
    obj.insert(QStringLiteral("end"), end);

    insertRemindersIfSet(obj, request);

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QNetworkRequest GoogleCalendarApi::authorizedRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_authManager->accessToken().toUtf8());
    request.setTransferTimeout(kNetworkTimeoutMs);
    return request;
}

void GoogleCalendarApi::fetchCalendarList()
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit calendarListFetchFailed(tr("You are not signed in."));
        return;
    }

    QNetworkReply *reply = m_network->get(authorizedRequest(kCalendarListEndpoint));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (status == 401) {
            emit calendarListFetchFailed(tr("Your session may have expired. Please sign in again."));
            return;
        }

        if (body.isEmpty()) {
            emit calendarListFetchFailed(tr("Could not load calendars: %1").arg(reply->errorString()));
            return;
        }

        QString error;
        const std::optional<QList<Calendar>> calendars = Calendar::listFromJson(body, &error);
        if (!calendars) {
            emit calendarListFetchFailed(tr("Could not load calendars: %1").arg(extractApiErrorMessage(body, status)));
            return;
        }

        emit calendarListFetched(*calendars);
    });
}

void GoogleCalendarApi::setCalendarSelected(const QString &calendarId, bool selected)
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit calendarSelectedChangeFailed(calendarId, !selected, tr("You are not signed in."));
        return;
    }

    QNetworkRequest request = authorizedRequest(QUrl(calendarListEntryUrl(calendarId)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_network->sendCustomRequest(request, "PATCH", buildSelectedPatchBody(selected));
    connect(reply, &QNetworkReply::finished, this, [this, reply, calendarId, selected] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300)
            return;

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = tr("Your session may have expired. Please sign in again.");
        else if (body.isEmpty())
            message = tr("Could not update calendar visibility: %1").arg(reply->errorString());
        else
            message = tr("Could not update calendar visibility: %1").arg(extractApiErrorMessage(body, status));
        emit calendarSelectedChangeFailed(calendarId, !selected, message);
    });
}

quint64 GoogleCalendarApi::fetchEvents(const QString &calendarId, const QString &timeMinRfc3339, const QString &timeMaxRfc3339)
{
    const quint64 requestId = m_nextFetchRequestId++;

    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit eventsFetchFailed(requestId, calendarId, tr("You are not signed in."));
        return requestId;
    }

    fetchEventsPage(requestId, calendarId, timeMinRfc3339, timeMaxRfc3339, QString(), {});
    return requestId;
}

void GoogleCalendarApi::fetchEventsPage(quint64 requestId, const QString &calendarId,
                                         const QString &timeMinRfc3339, const QString &timeMaxRfc3339,
                                         const QString &pageToken, QList<Event> accumulated)
{
    const QUrl url = buildEventsListUrl(calendarId, timeMinRfc3339, timeMaxRfc3339, pageToken);
    QNetworkReply *reply = m_network->get(authorizedRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, calendarId, timeMinRfc3339, timeMaxRfc3339, accumulated]() mutable {
                reply->deleteLater();

                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = reply->readAll();

                if (status == 401) {
                    emit eventsFetchFailed(requestId, calendarId, tr("Your session may have expired. Please sign in again."));
                    return;
                }

                if (body.isEmpty()) {
                    emit eventsFetchFailed(requestId, calendarId, tr("Could not load events: %1").arg(reply->errorString()));
                    return;
                }

                QString nextPageToken;
                QString error;
                const std::optional<QList<Event>> page = Event::listFromJson(body, calendarId, &nextPageToken, &error);
                if (!page) {
                    emit eventsFetchFailed(requestId, calendarId, tr("Could not load events: %1").arg(extractApiErrorMessage(body, status)));
                    return;
                }

                accumulated.append(*page);

                if (!nextPageToken.isEmpty()) {
                    fetchEventsPage(requestId, calendarId, timeMinRfc3339, timeMaxRfc3339, nextPageToken, accumulated);
                    return;
                }

                emit eventsFetched(requestId, calendarId, accumulated);
            });
}

void GoogleCalendarApi::createEvent(quint64 requestId, const NewEventRequest &request)
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit eventCreateFailed(requestId, request.calendarId, tr("You are not signed in."));
        return;
    }

    QNetworkRequest networkRequest = authorizedRequest(QUrl(calendarEventsCollectionUrl(request.calendarId)));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_network->post(networkRequest, buildCreateEventBody(request));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, calendarId = request.calendarId] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            emit eventCreated(requestId, calendarId);
            return;
        }

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = tr("Your session may have expired. Please sign in again.");
        else if (status == 403)
            message = tr("You don't have permission to add events to this calendar: %1").arg(extractApiErrorMessage(body, status));
        else if (body.isEmpty())
            message = tr("Could not create event: %1").arg(reply->errorString());
        else
            message = tr("Could not create event: %1").arg(extractApiErrorMessage(body, status));
        emit eventCreateFailed(requestId, calendarId, message);
    });
}

void GoogleCalendarApi::updateEvent(quint64 requestId, const QString &eventId, const NewEventRequest &request)
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit eventUpdateFailed(requestId, request.calendarId, eventId, tr("You are not signed in."));
        return;
    }

    QNetworkRequest networkRequest = authorizedRequest(buildEventDetailUrl(request.calendarId, eventId));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_network->sendCustomRequest(networkRequest, "PATCH", buildUpdateEventBody(request));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, eventId, calendarId = request.calendarId] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            emit eventUpdated(requestId, calendarId, eventId);
            return;
        }

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = tr("Your session may have expired. Please sign in again.");
        else if (status == 403)
            message = tr("You don't have permission to edit this event: %1").arg(extractApiErrorMessage(body, status));
        else if (status == 404)
            message = tr("This event no longer exists: %1").arg(extractApiErrorMessage(body, status));
        else if (body.isEmpty())
            message = tr("Could not update event: %1").arg(reply->errorString());
        else
            message = tr("Could not update event: %1").arg(extractApiErrorMessage(body, status));
        emit eventUpdateFailed(requestId, calendarId, eventId, message);
    });
}

void GoogleCalendarApi::deleteEvent(quint64 requestId, const QString &calendarId, const QString &eventId)
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit eventDeleteFailed(requestId, calendarId, eventId, tr("You are not signed in."));
        return;
    }

    const QNetworkRequest networkRequest = authorizedRequest(buildEventDetailUrl(calendarId, eventId));

    QNetworkReply *reply = m_network->sendCustomRequest(networkRequest, "DELETE");
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, calendarId, eventId] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 410 Gone means the event was already deleted server-side; Google's
        // own guidance treats this as "no action needed," so it's a soft
        // success here too, not a hard error.
        if ((status >= 200 && status < 300) || status == 410) {
            emit eventDeleted(requestId, calendarId, eventId);
            return;
        }

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = tr("Your session may have expired. Please sign in again.");
        else if (status == 403)
            message = tr("You don't have permission to delete this event: %1").arg(extractApiErrorMessage(body, status));
        else if (status == 404)
            message = tr("This event no longer exists: %1").arg(extractApiErrorMessage(body, status));
        else if (body.isEmpty())
            message = tr("Could not delete event: %1").arg(reply->errorString());
        else
            message = tr("Could not delete event: %1").arg(extractApiErrorMessage(body, status));
        emit eventDeleteFailed(requestId, calendarId, eventId, message);
    });
}
