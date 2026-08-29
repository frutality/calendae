#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include "credentialsprovider.h"
#include "tokenresponse.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QUrlQuery;
class QWidget;
QT_END_NAMESPACE

class OAuthLoopbackServer;

// Orchestrates Google OAuth 2.0 authentication for the app's single
// supported account: silent session restore on startup, interactive
// sign-in via the system browser (RFC 8252 loopback flow), sign-out, and
// keeping the access token warm while signed in. Never calls the Calendar
// API itself.
class AuthManager : public QObject
{
    Q_OBJECT
public:
    enum class AuthState {
        SignedOut,
        Restoring,
        SigningIn,
        SignedIn,
    };
    Q_ENUM(AuthState)

    explicit AuthManager(QObject *parent = nullptr);

    AuthState state() const { return m_state; }

    // Valid only while state() == SignedIn.
    QString accessToken() const { return m_accessToken; }

    // A stable, non-reversible per-account identifier (hex SHA-256 of the
    // current refresh token), for namespacing on-disk caches so one
    // account's data can't be shown for another. Empty when no refresh
    // token is held. Note: signing out and back in mints a new refresh
    // token, hence a new key — acceptable, since sign-out wipes the cache
    // anyway and stale directories are pruned by age.
    QString accountKey() const;

    // Exposed for unit testing: pure construction of the Google
    // authorization-endpoint URL, no I/O involved.
    static QUrl buildAuthorizationUrl(const OAuthClientCredentials &credentials,
                                       quint16 loopbackPort,
                                       const QByteArray &codeChallenge,
                                       const QString &state);

public slots:
    // Attempts to silently restore a previous session from a stored
    // refresh token. Never shows any UI. Call once at startup.
    void restoreSession();

    // Runs the full interactive OAuth loopback flow, possibly showing the
    // first-run credentials dialog (parented to dialogParent) and opening
    // the system browser.
    void signIn(QWidget *dialogParent);

    void signOut();

signals:
    void stateChanged(AuthManager::AuthState state);
    void signedIn();
    void signedOut();
    void errorOccurred(const QString &message);

private:
    enum class RefreshContext {
        Restore,
        Proactive,
    };

    void setState(AuthState state);
    void resolveCredentials(bool allowInteractiveFallback,
                             QWidget *dialogParent,
                             const std::function<void(const OAuthClientCredentials &)> &onResolved,
                             const std::function<void(const QString &)> &onFailed);

    void loadStoredRefreshTokenThenRestore();
    void refreshAccessToken(RefreshContext context);
    void exchangeAuthorizationCode(const QString &code);
    void handleTokenReply(QNetworkReply *reply, const std::function<void(const TokenResponse &)> &onSuccess,
                           const std::function<void(const QString &)> &onFailure);

    void beginSignInFlow();
    void cleanupLoopbackServer();
    void scheduleProactiveRefresh();
    void saveRefreshToken(const QString &token);
    void deleteStoredRefreshToken();
    void revokeStoredRefreshTokenBestEffort();

    QNetworkReply *postForm(const QUrl &endpoint, const QUrlQuery &params);

    QNetworkAccessManager *m_network;
    OAuthLoopbackServer *m_loopbackServer = nullptr;

    AuthState m_state = AuthState::SignedOut;

    OAuthClientCredentials m_credentials;
    QString m_accessToken;
    QDateTime m_accessTokenExpiryUtc;
    QString m_refreshToken;

    QByteArray m_pkceVerifier;
    QString m_expectedState;
    QString m_redirectUri;

    QTimer m_proactiveRefreshTimer;
    QTimer m_signInTimeoutTimer;
};

#endif // AUTHMANAGER_H
