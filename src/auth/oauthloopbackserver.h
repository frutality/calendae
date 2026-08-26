#ifndef OAUTHLOOPBACKSERVER_H
#define OAUTHLOOPBACKSERVER_H

#include <QObject>

QT_BEGIN_NAMESPACE
class QTcpServer;
class QTcpSocket;
QT_END_NAMESPACE

// A single-shot local HTTP server used as the redirect target for the OAuth
// 2.0 loopback flow (RFC 8252): binds 127.0.0.1 on an ephemeral port, waits
// for exactly one GET request carrying the authorization result, replies
// with a short HTML page, and stops listening.
class OAuthLoopbackServer : public QObject
{
    Q_OBJECT
public:
    explicit OAuthLoopbackServer(QObject *parent = nullptr);

    bool listen();
    quint16 port() const;
    void close();

signals:
    void authorizationReceived(const QString &code, const QString &state);
    void authorizationError(const QString &error, const QString &state);

private:
    void onNewConnection();
    void handleRequestLine(QTcpSocket *socket, const QByteArray &requestLine);

    QTcpServer *m_server;
};

#endif // OAUTHLOOPBACKSERVER_H
