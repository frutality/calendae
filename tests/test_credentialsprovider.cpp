#include "auth/credentialsprovider.h"
#include "fakekeychainbackend.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWidget>

// IMPORTANT: every CredentialsProvider constructed here for the keychain
// paths must be given an explicit FakeKeychainBackend (never leave the
// second constructor argument defaulted to nullptr) — see
// fakekeychainbackend.h and test_authmanager.cpp's top-of-file note for why.
class TestCredentialsProvider : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();

    void resolvesFromConfigFile();
    void resolvesFromKeychainWhenNoConfigFile();
    void failsWhenNothingIsConfiguredAndNoInteractiveFallback();
    void showsDialogAndSavesToKeychainWhenInteractiveFallbackAllowed();
    void cancellingTheDialogFails();
};

void TestCredentialsProvider::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestCredentialsProvider::init()
{
    // Every test starts with a clean slate: no leftover config file from a
    // previous test forcing tryConfigFile() to succeed unexpectedly.
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .remove(QStringLiteral("oauth_client.json"));
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
}

void TestCredentialsProvider::resolvesFromKeychainWhenNoConfigFile()
{
    FakeKeychainBackend keychain;
    const QJsonObject obj{
        {QStringLiteral("client_id"), QStringLiteral("keychain-client-id")},
        {QStringLiteral("client_secret"), QStringLiteral("keychain-client-secret")},
    };
    keychain.entries[CredentialsProvider::keychainKey] = QString::fromUtf8(QJsonDocument(obj).toJson());

    CredentialsProvider provider(nullptr, &keychain);
    QSignalSpy resolvedSpy(&provider, &CredentialsProvider::resolved);

    provider.resolve(/*allowInteractiveFallback=*/false);

    QVERIFY(resolvedSpy.wait());
    const auto creds = resolvedSpy.first().first().value<OAuthClientCredentials>();
    QCOMPARE(creds.clientId, QStringLiteral("keychain-client-id"));
    QCOMPARE(creds.clientSecret, QStringLiteral("keychain-client-secret"));
}

void TestCredentialsProvider::failsWhenNothingIsConfiguredAndNoInteractiveFallback()
{
    FakeKeychainBackend keychain; // empty: neither config file nor keychain has anything

    CredentialsProvider provider(nullptr, &keychain);
    QSignalSpy failedSpy(&provider, &CredentialsProvider::failed);

    provider.resolve(/*allowInteractiveFallback=*/false);

    QVERIFY(failedSpy.wait());
    QCOMPARE(failedSpy.first().first().toString(), QStringLiteral("No Google OAuth client credentials are configured."));
}

void TestCredentialsProvider::showsDialogAndSavesToKeychainWhenInteractiveFallbackAllowed()
{
    FakeKeychainBackend keychain;
    QWidget dialogParent;

    CredentialsProvider provider(nullptr, &keychain);
    QSignalSpy resolvedSpy(&provider, &CredentialsProvider::resolved);

    provider.resolve(/*allowInteractiveFallback=*/true, &dialogParent);

    QDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = dialogParent.findChild<QDialog *>()));
    dialog->findChild<QLineEdit *>(QStringLiteral("clientIdEdit"))->setText(QStringLiteral("typed-id"));
    dialog->findChild<QLineEdit *>(QStringLiteral("clientSecretEdit"))->setText(QStringLiteral("typed-secret"));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();

    // click() -> accept() -> finished() -> resolved() all chain
    // synchronously, so the signal has already fired by the time control
    // returns here — nothing left to wait() for.
    QCOMPARE(resolvedSpy.count(), 1);
    const auto creds = resolvedSpy.first().first().value<OAuthClientCredentials>();
    QCOMPARE(creds.clientId, QStringLiteral("typed-id"));
    QCOMPARE(creds.clientSecret, QStringLiteral("typed-secret"));

    // The dialog's write to the keychain isn't tied to the dialog/provider's
    // own lifetime (see CredentialsProvider::showDialogAndSave), but
    // FakeKeychainBackend still applies it synchronously.
    const QJsonDocument saved = QJsonDocument::fromJson(keychain.entries.value(CredentialsProvider::keychainKey).toUtf8());
    QCOMPARE(saved.object().value(QStringLiteral("client_id")).toString(), QStringLiteral("typed-id"));
    QCOMPARE(saved.object().value(QStringLiteral("client_secret")).toString(), QStringLiteral("typed-secret"));
}

void TestCredentialsProvider::cancellingTheDialogFails()
{
    FakeKeychainBackend keychain;
    QWidget dialogParent;

    CredentialsProvider provider(nullptr, &keychain);
    QSignalSpy failedSpy(&provider, &CredentialsProvider::failed);

    provider.resolve(/*allowInteractiveFallback=*/true, &dialogParent);

    QDialog *dialog = nullptr;
    QTRY_VERIFY((dialog = dialogParent.findChild<QDialog *>()));
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();

    QCOMPARE(failedSpy.count(), 1); // synchronous chain, same as the Ok path above
    QCOMPARE(failedSpy.first().first().toString(), QStringLiteral("Sign-in requires Google OAuth client credentials."));
    QVERIFY(!keychain.entries.contains(CredentialsProvider::keychainKey));
}

QTEST_MAIN(TestCredentialsProvider)
#include "test_credentialsprovider.moc"
