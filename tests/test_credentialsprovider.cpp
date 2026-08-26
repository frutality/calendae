#include "auth/credentialsprovider.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Only the config-file resolution path is covered here: it is fully
// sandboxed via QStandardPaths::setTestModeOn() and never touches the real
// system keychain, unlike the QtKeychain-backed fallback paths.
class TestCredentialsProvider : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void resolvesFromConfigFile();
};

void TestCredentialsProvider::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestCredentialsProvider::resolvesFromConfigFile()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QVERIFY(QDir().mkpath(configDir));

    QFile file(configDir + QStringLiteral("/oauth_client.json"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject obj{
        {QStringLiteral("client_id"), QStringLiteral("test-client-id")},
        {QStringLiteral("client_secret"), QStringLiteral("test-client-secret")},
    };
    file.write(QJsonDocument(obj).toJson());
    file.close();

    CredentialsProvider provider;
    QSignalSpy resolvedSpy(&provider, &CredentialsProvider::resolved);
    QSignalSpy failedSpy(&provider, &CredentialsProvider::failed);

    QString capturedClientId;
    QString capturedClientSecret;
    connect(&provider, &CredentialsProvider::resolved, this, [&](const OAuthClientCredentials &creds) {
        capturedClientId = creds.clientId;
        capturedClientSecret = creds.clientSecret;
    });

    provider.resolve(/*allowInteractiveFallback=*/false);

    QVERIFY(resolvedSpy.wait());
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(capturedClientId, QStringLiteral("test-client-id"));
    QCOMPARE(capturedClientSecret, QStringLiteral("test-client-secret"));

    file.remove();
}

QTEST_GUILESS_MAIN(TestCredentialsProvider)
#include "test_credentialsprovider.moc"
