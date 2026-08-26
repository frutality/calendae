#include "oauthloopbackserver.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

OAuthLoopbackServer::OAuthLoopbackServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &OAuthLoopbackServer::onNewConnection);
}

bool OAuthLoopbackServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 OAuthLoopbackServer::port() const
{
    return m_server->serverPort();
}

void OAuthLoopbackServer::close()
{
    m_server->close();
}

void OAuthLoopbackServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (!socket->canReadLine())
                return;
            const QByteArray requestLine = socket->readLine();
            handleRequestLine(socket, requestLine);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void OAuthLoopbackServer::handleRequestLine(QTcpSocket *socket, const QByteArray &requestLine)
{
    // Expected form: "GET /callback?code=...&state=... HTTP/1.1\r\n"
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        socket->disconnectFromHost();
        return;
    }

    const QUrl url(QStringLiteral("http://127.0.0.1") + QString::fromUtf8(parts.at(1)));
    const QUrlQuery query(url.query());

    const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    const QString state = query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
    const QString error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);

    if (code.isEmpty() && error.isEmpty()) {
        // Not the OAuth redirect (e.g. a stray favicon fetch) — answer
        // minimally and keep listening for the real one.
        socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    const QString body = error.isEmpty()
        ? tr("Signed in to Calendae. You can close this tab and return to the app.")
        : tr("Sign-in to Calendae failed. You can close this tab and return to the app.");

    const QByteArray bodyUtf8 = body.toUtf8();
    const QByteArray response = "HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/html; charset=utf-8\r\n"
                                 "Content-Length: "
        + QByteArray::number(bodyUtf8.size()) + "\r\n"
                                                 "Connection: close\r\n\r\n"
        + bodyUtf8;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();

    close();

    if (!error.isEmpty())
        emit authorizationError(error, state);
    else
        emit authorizationReceived(code, state);
}
