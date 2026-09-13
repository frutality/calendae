#ifndef AUTHTESTHELPERS_H
#define AUTHTESTHELPERS_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>

// Shared setup for test_authmanager and test_googlecalendarapi.
//
// IMPORTANT: nothing here may touch QtKeychain, even indirectly (e.g. by
// calling AuthManager::restoreSession()/signIn() through to a success path
// that saves/reads/deletes a token). CredentialsProvider::keychainService
// ("calendae") is the same service name the real app uses, and on a machine
// with a real OS keyring (Secret Service/libsecret, KWallet), QtKeychain's
// setInsecureFallback(true) is NOT consulted for a plain "not found" or a
// successful read/write/delete — those go straight to the real backend. A
// previous version of these tests seeded/cleared a QSettings-based
// "fallback store" believing it was sandboxed; it wasn't, and running them
// deleted a real developer's real stored Google refresh token. Use
// AuthManager::setSignedInForTesting() instead of restoreSession()/signIn()
// wherever a SignedIn AuthManager is needed.
namespace AuthTestHelpers {

// Makes CredentialsProvider::resolve() succeed from the config file alone,
// without ever hitting the keychain or showing the first-run dialog. Safe:
// the config file lives under QStandardPaths::AppConfigLocation, sandboxed
// by QStandardPaths::setTestModeEnabled(true), and is unrelated to the
// keychain.
inline void seedCredentialsConfigFile(const QString &clientId = QStringLiteral("test-client-id"),
                                       const QString &clientSecret = QStringLiteral("test-client-secret"))
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);

    QFile file(configDir + QStringLiteral("/oauth_client.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    const QJsonObject obj{
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("client_secret"), clientSecret},
    };
    file.write(QJsonDocument(obj).toJson());
}

// Builds a Google token-endpoint success JSON body.
inline QByteArray tokenResponseJson(const QString &accessToken, int expiresInSeconds = 3600,
                                     const QString &refreshToken = QString())
{
    QJsonObject obj{
        {QStringLiteral("access_token"), accessToken},
        {QStringLiteral("token_type"), QStringLiteral("Bearer")},
        {QStringLiteral("expires_in"), expiresInSeconds},
    };
    if (!refreshToken.isEmpty())
        obj.insert(QStringLiteral("refresh_token"), refreshToken);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

} // namespace AuthTestHelpers

#endif // AUTHTESTHELPERS_H
