#include "auth/credentialsprovider.h"
#include "auth/keychainjob.h"

#include <QByteArray>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>

// keychainSecretStoredInsecurely() only inspects a QSettings file, so it is
// fully sandboxed here via QStandardPaths test mode and never touches a real
// keyring -- same approach as test_credentialsprovider.
class TestKeychainJob : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void falseWhenFallbackStoreIsEmpty();
    void trueOnceASecretIsWrittenToTheFallback();
    void falseAgainAfterTheFallbackEntryIsRemoved();

private:
    static void clearFallbackStore();
    static void writeFallbackSecret(const QString &key);
};

void TestKeychainJob::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestKeychainJob::init()
{
    clearFallbackStore();
}

void TestKeychainJob::clearFallbackStore()
{
    QSettings settings(CredentialsProvider::keychainService);
    settings.clear();
    settings.sync();
}

void TestKeychainJob::writeFallbackSecret(const QString &key)
{
    // Mirror QtKeychain PlainTextStore's on-disk layout on Unix.
    QSettings settings(CredentialsProvider::keychainService);
    settings.setValue(key + QStringLiteral("/type"), QStringLiteral("text"));
    settings.setValue(key + QStringLiteral("/data"), QByteArrayLiteral("a-refresh-token"));
    settings.sync();
}

void TestKeychainJob::falseWhenFallbackStoreIsEmpty()
{
    QVERIFY(!keychainSecretStoredInsecurely());
}

void TestKeychainJob::trueOnceASecretIsWrittenToTheFallback()
{
    writeFallbackSecret(QStringLiteral("refresh_token"));
    QVERIFY(keychainSecretStoredInsecurely());
}

void TestKeychainJob::falseAgainAfterTheFallbackEntryIsRemoved()
{
    writeFallbackSecret(QStringLiteral("refresh_token"));
    QVERIFY(keychainSecretStoredInsecurely());

    QSettings settings(CredentialsProvider::keychainService);
    settings.remove(QStringLiteral("refresh_token"));
    settings.sync();

    QVERIFY(!keychainSecretStoredInsecurely());
}

QTEST_MAIN(TestKeychainJob)
#include "test_keychainjob.moc"
