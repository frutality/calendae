#include "googlecalendarapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace {
const QUrl kCalendarListEndpoint(QStringLiteral("https://www.googleapis.com/calendar/v3/users/me/calendarList"));

// A bounded [timeMin, timeMax) window can't legitimately need many pages
// (Google's default is 250 events/page). This only trips on an anomaly —
// a runaway calendar or a nextPageToken that never clears.
constexpr int kMaxEventPages = 40;

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
    , m_network(authManager->networkAccessManager())
{
}

QByteArray GoogleCalendarApi::buildSelectedPatchBody(bool selected)
{
    const QJsonObject obj{
        {QStringLiteral("selected"), selected},
    };
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

bool GoogleCalendarApi::isTransientNetworkError(QNetworkReply::NetworkError error, int httpStatusCode)
{
    if (httpStatusCode >= 400)
        return false; // the server answered (401/403/404/5xx) — not a connectivity problem

    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: // transfer timeout fires as this
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::BackgroundRequestNotAllowedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::UnknownProxyError:
        return true;
    default:
        return false;
    }
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

void GoogleCalendarApi::sendAuthorized(const std::function<QNetworkReply *()> &issue,
                                       const std::function<void(QNetworkReply *)> &onFinished)
{
    const auto complete = [onFinished](QNetworkReply *reply) {
        reply->deleteLater();
        onFinished(reply);
    };

    // The token this attempt is sent with, so AuthManager can tell a caller
    // whose token was already replaced from one that needs a real refresh.
    QNetworkReply *reply = issue();
    const QString usedToken = m_authManager->accessToken();

    connect(reply, &QNetworkReply::finished, this, [this, reply, usedToken, issue, complete] {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 401) {
            complete(reply);
            return;
        }

        m_authManager->refreshAfterRejection(
            usedToken, this, [this, reply, issue, complete](AuthManager::RefreshResult result) {
                if (result != AuthManager::RefreshResult::Refreshed) {
                    m_lastUnauthorizedResult = result;
                    complete(reply);
                    return;
                }

                reply->deleteLater();
                QNetworkReply *retry = issue();
                connect(retry, &QNetworkReply::finished, this, [this, retry, complete] {
                    // A brand-new token refused too: refreshing again can't help.
                    m_lastUnauthorizedResult = AuthManager::RefreshResult::NotRecoverable;
                    complete(retry);
                });
            });
    });
}

QString GoogleCalendarApi::unauthorizedMessage() const
{
    switch (m_lastUnauthorizedResult) {
    case AuthManager::RefreshResult::Unavailable:
        return tr("Couldn't reach Google to renew your session. Check your connection.");
    case AuthManager::RefreshResult::ServerError:
        return tr("Google couldn't renew your session right now. Will try again shortly.");
    default:
        return tr("Your session may have expired. Please sign in again.");
    }
}

bool GoogleCalendarApi::unauthorizedIsTransient() const
{
    return m_lastUnauthorizedResult == AuthManager::RefreshResult::Unavailable;
}

void GoogleCalendarApi::fetchCalendarList()
{
    if (m_authManager->state() != AuthManager::AuthState::SignedIn) {
        emit calendarListFetchFailed(tr("You are not signed in."), false);
        return;
    }

    sendAuthorized([this] { return m_network->get(authorizedRequest(kCalendarListEndpoint)); },
                   [this](QNetworkReply *reply) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (status == 401) {
            emit calendarListFetchFailed(unauthorizedMessage(), unauthorizedIsTransient());
            return;
        }

        if (body.isEmpty()) {
            emit calendarListFetchFailed(tr("Could not load calendars: %1").arg(reply->errorString()),
                                          isTransientNetworkError(reply->error(), status));
            return;
        }

        QString error;
        const std::optional<QList<Calendar>> calendars = Calendar::listFromJson(body, &error);
        if (!calendars) {
            emit calendarListFetchFailed(tr("Could not load calendars: %1").arg(extractApiErrorMessage(body, status)), false);
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

    const QUrl url(calendarListEntryUrl(calendarId));
    const QByteArray patchBody = buildSelectedPatchBody(selected);
    sendAuthorized(
        [this, url, patchBody] {
            QNetworkRequest request = authorizedRequest(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            return m_network->sendCustomRequest(request, "PATCH", patchBody);
        },
        [this, calendarId, selected](QNetworkReply *reply) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300)
            return;

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = unauthorizedMessage();
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
        emit eventsFetchFailed(requestId, calendarId, tr("You are not signed in."), false);
        return requestId;
    }

    fetchEventsPage(requestId, calendarId, timeMinRfc3339, timeMaxRfc3339, QString(), {});
    return requestId;
}

void GoogleCalendarApi::fetchEventsPage(quint64 requestId, const QString &calendarId,
                                         const QString &timeMinRfc3339, const QString &timeMaxRfc3339,
                                         const QString &pageToken, QList<Event> accumulated, int pagesFetched)
{
    const QUrl url = buildEventsListUrl(calendarId, timeMinRfc3339, timeMaxRfc3339, pageToken);
    sendAuthorized(
        [this, url] { return m_network->get(authorizedRequest(url)); },
        [this, requestId, calendarId, timeMinRfc3339, timeMaxRfc3339,
         accumulated = std::move(accumulated), pagesFetched](QNetworkReply *reply) mutable {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = reply->readAll();

                if (status == 401) {
                    emit eventsFetchFailed(requestId, calendarId, unauthorizedMessage(),
                                            unauthorizedIsTransient());
                    return;
                }

                if (body.isEmpty()) {
                    emit eventsFetchFailed(requestId, calendarId,
                                            tr("Could not load events: %1").arg(reply->errorString()),
                                            isTransientNetworkError(reply->error(), status));
                    return;
                }

                QString nextPageToken;
                QString error;
                const std::optional<QList<Event>> page = Event::listFromJson(body, calendarId, &nextPageToken, &error);
                if (!page) {
                    emit eventsFetchFailed(requestId, calendarId,
                                            tr("Could not load events: %1").arg(extractApiErrorMessage(body, status)), false);
                    return;
                }

                accumulated.append(*page);

                if (!nextPageToken.isEmpty()) {
                    if (pagesFetched + 1 >= kMaxEventPages) {
                        emit eventsFetchFailed(requestId, calendarId,
                                                tr("Could not load events: too many result pages."), false);
                        return;
                    }
                    fetchEventsPage(requestId, calendarId, timeMinRfc3339, timeMaxRfc3339,
                                    nextPageToken, std::move(accumulated), pagesFetched + 1);
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

    const QUrl url(calendarEventsCollectionUrl(request.calendarId));
    const QByteArray requestBody = buildCreateEventBody(request);
    sendAuthorized(
        [this, url, requestBody] {
            QNetworkRequest networkRequest = authorizedRequest(url);
            networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            return m_network->post(networkRequest, requestBody);
        },
        [this, requestId, calendarId = request.calendarId](QNetworkReply *reply) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            emit eventCreated(requestId, calendarId);
            return;
        }

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = unauthorizedMessage();
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

    const QUrl url = buildEventDetailUrl(request.calendarId, eventId);
    const QByteArray requestBody = buildUpdateEventBody(request);
    sendAuthorized(
        [this, url, requestBody] {
            QNetworkRequest networkRequest = authorizedRequest(url);
            networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            return m_network->sendCustomRequest(networkRequest, "PATCH", requestBody);
        },
        [this, requestId, eventId, calendarId = request.calendarId](QNetworkReply *reply) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            emit eventUpdated(requestId, calendarId, eventId);
            return;
        }

        const QByteArray body = reply->readAll();
        QString message;
        if (status == 401)
            message = unauthorizedMessage();
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

    const QUrl url = buildEventDetailUrl(calendarId, eventId);
    sendAuthorized(
        [this, url] { return m_network->sendCustomRequest(authorizedRequest(url), "DELETE"); },
        [this, requestId, calendarId, eventId](QNetworkReply *reply) {
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
            message = unauthorizedMessage();
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
