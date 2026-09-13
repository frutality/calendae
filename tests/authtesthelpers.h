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
// Nothing here touches QtKeychain — that's now handled by injecting a
// FakeKeychainBackend (see fakekeychainbackend.h) into AuthManager/
// CredentialsProvider instead. Never let a test construct either with the
// keychain constructor argument defaulted to nullptr: that constructs a
// RealKeychainBackend, which talks to the actual OS credential store under
// the exact service name ("calendae") the real installed app uses. An
// earlier version of these tests tried to sandbox that via a QSettings-based
// "insecure fallback" file instead, believing it was isolated; it wasn't
// (a real keyring backend, when reachable, is used directly regardless of
// setInsecureFallback()), and running them once deleted a developer's real
// stored Google refresh token.
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

inline QByteArray tokenErrorJson(const QString &error, const QString &description = QString())
{
    QJsonObject obj{{QStringLiteral("error"), error}};
    if (!description.isEmpty())
        obj.insert(QStringLiteral("error_description"), description);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

} // namespace AuthTestHelpers

#endif // AUTHTESTHELPERS_H
