#ifndef FAKENETWORKACCESSMANAGER_H
#define FAKENETWORKACCESSMANAGER_H

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <cstring>
#include <functional>

// A QNetworkReply that never touches the network: its body/status/error are
// fixed at construction and delivered asynchronously (next event-loop turn,
// via FakeNetworkAccessManager) so callers that connect to finished() right
// after issuing the request — as all production code here does — still see
// it, exactly as they would with a real QNetworkAccessManager.
class FakeNetworkReply : public QNetworkReply
{
public:
    struct Response
    {
        int httpStatus = 200; // 0 means "no HTTP response at all" (connectivity failure)
        QByteArray body;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        QString errorString;
    };

    FakeNetworkReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setOperation(op);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }

    void complete(const Response &response)
    {
        m_body = response.body;
        if (response.httpStatus > 0)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.httpStatus);
        if (response.error != QNetworkReply::NoError)
            setError(response.error, response.errorString);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void abort() override { setFinished(true); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_body.size() - m_offset;
        const qint64 toRead = std::min(remaining, maxSize);
        if (toRead > 0) {
            std::memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(toRead));
            m_offset += toRead;
        }
        return toRead;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

// Stands in for QNetworkAccessManager: every get/post/sendCustomRequest is
// recorded and answered by `handler` (or, when unset, a canned 200 empty
// body) instead of going anywhere near the network. Point AuthManager's
// test-only constructor parameter at an instance of this to exercise its
// (and, transitively, GoogleCalendarApi's) real request-sending code paths
// deterministically.
class FakeNetworkAccessManager : public QNetworkAccessManager
{
public:
    struct RecordedRequest
    {
        QNetworkAccessManager::Operation operation;
        QString verb; // "PATCH"/"DELETE"/... for CustomOperation, empty otherwise
        QUrl url;
        QByteArray body;
    };

    std::function<FakeNetworkReply::Response(const RecordedRequest &)> handler =
        [](const RecordedRequest &) { return FakeNetworkReply::Response{}; };

    QList<RecordedRequest> requests;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                  QIODevice *outgoingData = nullptr) override
    {
        QByteArray body;
        if (outgoingData) {
            outgoingData->open(QIODevice::ReadOnly);
            body = outgoingData->readAll();
        }

        QString verb;
        if (op == QNetworkAccessManager::CustomOperation)
            verb = QString::fromLatin1(request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray());

        const RecordedRequest recorded{op, verb, request.url(), body};
        requests.append(recorded);

        auto *reply = new FakeNetworkReply(op, request, this);
        const FakeNetworkReply::Response response = handler(recorded);
        QTimer::singleShot(0, reply, [reply, response] { reply->complete(response); });
        return reply;
    }
};

#endif // FAKENETWORKACCESSMANAGER_H
