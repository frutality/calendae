#include "auth/oauthloopbackserver.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

class TestOAuthLoopbackServer : public QObject
{
    Q_OBJECT
private slots:
    void deliversAuthorizationCode();
    void deliversAuthorizationError();
};

void TestOAuthLoopbackServer::deliversAuthorizationCode()
{
    OAuthLoopbackServer server;
    QVERIFY(server.listen());

    QSignalSpy receivedSpy(&server, &OAuthLoopbackServer::authorizationReceived);

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(socket.waitForConnected());
    socket.write("GET /callback?code=abc123&state=xyz789 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    QVERIFY(socket.waitForBytesWritten());

    QVERIFY(receivedSpy.wait());
    QCOMPARE(receivedSpy.at(0).at(0).toString(), QStringLiteral("abc123"));
    QCOMPARE(receivedSpy.at(0).at(1).toString(), QStringLiteral("xyz789"));
}

void TestOAuthLoopbackServer::deliversAuthorizationError()
{
    OAuthLoopbackServer server;
    QVERIFY(server.listen());

    QSignalSpy errorSpy(&server, &OAuthLoopbackServer::authorizationError);

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(socket.waitForConnected());
    socket.write("GET /callback?error=access_denied&state=xyz789 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    QVERIFY(socket.waitForBytesWritten());

    QVERIFY(errorSpy.wait());
    QCOMPARE(errorSpy.at(0).at(0).toString(), QStringLiteral("access_denied"));
}

QTEST_GUILESS_MAIN(TestOAuthLoopbackServer)
#include "test_oauthloopbackserver.moc"
