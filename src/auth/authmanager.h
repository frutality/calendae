#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include "credentialsprovider.h"
#include "tokenresponse.h"

#include <keychain.h>

#include <QDateTime>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
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

    // Why there is currently no active session. Lets the UI explain the
    // auth gate ("waiting for a connection" vs. "session expired") and
    // decide whether a silent restore retry is worth attempting, instead
    // of one generic "not signed in".
    enum class SignOutReason {
        None,                     // never signed in, or an explicit sign-out
        NetworkUnavailable,       // couldn't reach Google to restore — a later retry may succeed
        CredentialStoreUnavailable, // the OS credential store wasn't reachable (e.g. KWallet/D-Bus
                                  // not up yet at login autostart) — a background retry may succeed
        SessionExpired,           // the stored refresh token was rejected — interactive sign-in required
        SignInFailed,             // an interactive sign-in didn't complete
    };
    Q_ENUM(SignOutReason)

    explicit AuthManager(QObject *parent = nullptr);

    AuthState state() const { return m_state; }

    // Meaningful whenever state() == SignedOut.
    SignOutReason lastSignOutReason() const { return m_lastSignOutReason; }

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

    // How a failed token-endpoint reply (exchange or refresh) should be
    // treated. Only InvalidGrant means the stored refresh token is
    // genuinely dead and must be discarded; every other outcome leaves it
    // on disk so a network blip can't force a re-authentication.
    enum class TokenFailure {
        InvalidGrant, // HTTP 400 { "error": "invalid_grant" } — revoked/expired
        Transient,    // connectivity failure, no HTTP response
        ServerError,  // the server answered, but not with invalid_grant (5xx, 429, invalid_client, malformed)
    };

    // Exposed for unit testing: pure classification, no I/O.
    static TokenFailure classifyTokenFailure(QNetworkReply::NetworkError error,
                                              int httpStatusCode,
                                              const QByteArray &body);

    // How a failed keychain read of the stored refresh token during startup
    // restore should be handled. Only NoSession means nothing was ever
    // stored; a backend that's momentarily unavailable (KWallet daemon / the
    // D-Bus session bus not up yet at autostart) must NOT be mistaken for
    // "signed out" and lose a valid session.
    enum class KeychainReadOutcome {
        NoSession, // EntryNotFound — no token has ever been stored
        Retriable, // the store is momentarily unavailable — try again shortly
        Fatal,     // user refused to unlock, or the platform has no backend
    };

    // Exposed for unit testing: pure classification, no I/O.
    static KeychainReadOutcome classifyKeychainReadError(QKeychain::Error error);

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

    void readStoredRefreshToken();
    void loadStoredRefreshTokenThenRestore();
    void refreshAccessToken(RefreshContext context);
    void exchangeAuthorizationCode(const QString &code);
    void handleTokenReply(QNetworkReply *reply, const std::function<void(const TokenResponse &)> &onSuccess,
                           const std::function<void(TokenFailure, const QString &)> &onFailure);

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
    SignOutReason m_lastSignOutReason = SignOutReason::None;

    OAuthClientCredentials m_credentials;
    QString m_accessToken;
    QDateTime m_accessTokenExpiryUtc;
    QString m_refreshToken;

    QByteArray m_pkceVerifier;
    QString m_expectedState;
    QString m_redirectUri;

    // Bumped whenever a new auth attempt begins (restore, interactive
    // sign-in) or the session is torn down (sign-out). An in-flight token
    // reply captures the value at request time and drops itself on
    // completion if it no longer matches, so a slow reply can't resurrect a
    // signed-out session or clobber the keychain after sign-out.
    quint64 m_authEpoch = 0;

    // Retriable keychain-read failures during restore are retried a bounded
    // number of times before giving up (see readStoredRefreshToken).
    int m_keychainReadAttempts = 0;

    QTimer m_proactiveRefreshTimer;
    QTimer m_signInTimeoutTimer;
};

#endif // AUTHMANAGER_H
