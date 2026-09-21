#ifndef GOOGLECALENDARAPI_H
#define GOOGLECALENDARAPI_H

#include "auth/authmanager.h"
#include "calendar.h"
#include "event.h"
#include "neweventrequest.h"

#include <QNetworkReply>
#include <QObject>

#include <functional>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkRequest;
QT_END_NAMESPACE

// Thin wrapper over the Google Calendar REST API's calendarList endpoints.
// Reads the bearer token from AuthManager on every request (never caches
// it), so it always uses whatever AuthManager currently holds. It does not
// refresh tokens itself either: AuthManager's proactive refresh (60s before
// expiry) keeps the token valid in normal use, but that timer is monotonic
// and stands still while the machine is suspended, so the token can be
// expired on wake. A 401 therefore makes the request ask
// AuthManager::refreshAfterRejection() for a new access token and be
// re-sent once. Only if that cannot help (or the retry is refused too) does
// the 401 become the user-visible "session may have expired" error; a
// failure to *reach* the token endpoint is reported as such, not as a dead
// session.
class GoogleCalendarApi : public QObject
{
    Q_OBJECT
public:
    explicit GoogleCalendarApi(AuthManager *authManager, QObject *parent = nullptr);

    // Pure/deterministic helpers, exposed for unit testing without network I/O.
    static QByteArray buildSelectedPatchBody(bool selected);
    static QString extractApiErrorMessage(const QByteArray &body, int httpStatusCode);
    static QUrl buildEventsListUrl(const QString &calendarId, const QString &timeMinRfc3339,
                                    const QString &timeMaxRfc3339, const QString &pageToken = QString());
    static QByteArray buildCreateEventBody(const NewEventRequest &request);
    static QUrl buildEventDetailUrl(const QString &calendarId, const QString &eventId);
    static QByteArray buildUpdateEventBody(const NewEventRequest &request);

    // True when a failed reply looks like "can't reach the server right now"
    // (connection refused, DNS failure, timeout, transient proxy/network
    // error) rather than a request the server actually answered. Drives the
    // "server unavailable, showing saved data" status and its quiet retry
    // loop; an HTTP status >= 400 (401/403/404/5xx) is never transient here —
    // the server replied, so retrying on a timer wouldn't help.
    static bool isTransientNetworkError(QNetworkReply::NetworkError error, int httpStatusCode);

public slots:
    // Fetches the first page only (Google default maxResults=100); does not
    // follow nextPageToken. Acceptable for a personal account's calendar
    // count; revisit if this ever needs to scale.
    void fetchCalendarList();

    // Optimistic-UI counterpart: caller has already updated its UI to
    // `selected` before calling this. On failure,
    // calendarSelectedChangeFailed reports the boolean to roll back to
    // (always !selected, since this is a binary toggle).
    void setCalendarSelected(const QString &calendarId, bool selected);

    // Assigns and returns a fresh requestId (echoed back unchanged in
    // eventsFetched/eventsFetchFailed, including across pagination pages of
    // the same logical fetch), so callers can use it to discard stale
    // replies. Follows nextPageToken until exhausted. The id is generated
    // here — not supplied by the caller — because this one GoogleCalendarApi
    // instance is shared by multiple independent controllers (month/week/day
    // views); caller-local counters would collide (two controllers both
    // handing out "1") and cause one controller's reply to be mistakenly
    // consumed by another's identically-numbered in-flight request.
    // virtual purely so unit tests can substitute a recording double for
    // the store/controllers without real network I/O.
    virtual quint64 fetchEvents(const QString &calendarId, const QString &timeMinRfc3339, const QString &timeMaxRfc3339);

    // requestId is opaque, echoed back unchanged in eventCreated/eventCreateFailed.
    void createEvent(quint64 requestId, const NewEventRequest &request);

    // requestId is opaque, echoed back unchanged in eventUpdated/
    // eventUpdateFailed. request.calendarId must be the event's current
    // calendar (this app never moves events between calendars — Google
    // models that as a separate events.move operation). eventId is passed
    // separately since NewEventRequest has no id field; for an occurrence
    // of a recurring event this is the instance's own id (e.g.
    // "abc123_20260825T090000Z"), which Google treats as patching just that
    // occurrence, leaving the master's recurrence rule untouched.
    void updateEvent(quint64 requestId, const QString &eventId, const NewEventRequest &request);

    // requestId is opaque, echoed back unchanged in eventDeleted/
    // eventDeleteFailed. eventId is the event's own id — for an occurrence
    // of a recurring event this is the instance's own id, which Google
    // treats as deleting just that occurrence, leaving the rest of the
    // series untouched. No request body; DELETE has none. A 410 response
    // ("already deleted") is treated as success, not failure: the end
    // state the caller wants — event gone — is already true, most likely
    // because it was deleted from another client or a duplicate delete
    // raced this one.
    void deleteEvent(quint64 requestId, const QString &calendarId, const QString &eventId);

signals:
    void calendarListFetched(const QList<Calendar> &calendars);
    void calendarListFetchFailed(const QString &message, bool transient);
    void calendarSelectedChangeFailed(const QString &calendarId, bool revertToSelected, const QString &message);
    void eventsFetched(quint64 requestId, const QString &calendarId, const QList<Event> &events);
    void eventsFetchFailed(quint64 requestId, const QString &calendarId, const QString &message, bool transient);
    void eventCreated(quint64 requestId, const QString &calendarId);
    void eventCreateFailed(quint64 requestId, const QString &calendarId, const QString &message);
    void eventUpdated(quint64 requestId, const QString &calendarId, const QString &eventId);
    void eventUpdateFailed(quint64 requestId, const QString &calendarId, const QString &eventId, const QString &message);
    void eventDeleted(quint64 requestId, const QString &calendarId, const QString &eventId);
    void eventDeleteFailed(quint64 requestId, const QString &calendarId, const QString &eventId, const QString &message);

private:
    QNetworkRequest authorizedRequest(const QUrl &url) const;

    // Sends via `issue` (which must build its request with
    // authorizedRequest() each time it is called, so a retry picks up the
    // new token) and hands the final reply to `onFinished`, which owns
    // neither deleting it nor retrying. A 401 triggers at most ONE refresh +
    // re-send per call: a 401 on the retry is final. Safe for POST/PATCH/
    // DELETE too — a 401 means the request was never executed.
    void sendAuthorized(const std::function<QNetworkReply *()> &issue,
                        const std::function<void(QNetworkReply *)> &onFinished);

    // Wording/classification for a final 401, per why refreshing didn't help.
    QString unauthorizedMessage() const;
    bool unauthorizedIsTransient() const;

    void fetchEventsPage(quint64 requestId, const QString &calendarId, const QString &timeMinRfc3339,
                          const QString &timeMaxRfc3339, const QString &pageToken, QList<Event> accumulated,
                          int pagesFetched = 0);

    AuthManager *m_authManager;
    QNetworkAccessManager *m_network; // borrowed from m_authManager, not owned
    quint64 m_nextFetchRequestId = 1;
    // Why the last 401 reaching a handler could not be recovered; read by
    // unauthorizedMessage()/unauthorizedIsTransient() while that handler runs.
    AuthManager::RefreshResult m_lastUnauthorizedResult = AuthManager::RefreshResult::NotRecoverable;
};

#endif // GOOGLECALENDARAPI_H
