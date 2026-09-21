#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include "credentialsprovider.h"
#include "keychainbackend.h"
#include "tokenresponse.h"

#include <keychain.h>

#include <QDateTime>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
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

    // `network`/`keychain`, when non-null, are used instead of a freshly
    // constructed real one — exists solely so unit tests can substitute
    // fakes that never touch the real network or OS credential store.
    // Production code always passes nullptr for both.
    explicit AuthManager(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr,
                         KeychainBackend *keychain = nullptr);

    AuthState state() const { return m_state; }

    // Meaningful whenever state() == SignedOut.
    SignOutReason lastSignOutReason() const { return m_lastSignOutReason; }

    // Valid only while state() == SignedIn.
    QString accessToken() const { return m_accessToken; }

    // Exposed for unit testing: jumps straight to SignedIn with the given
    // token, without going through restoreSession()/signIn() at all — handy
    // for tests (e.g. GoogleCalendarApi's) that just need a SignedIn
    // AuthManager and don't care how it got there.
    void setSignedInForTesting(const QString &accessToken)
    {
        m_accessToken = accessToken;
        setState(AuthState::SignedIn);
    }

    // The single process-wide QNetworkAccessManager. GoogleCalendarApi (and
    // everything layered on it) borrows this instead of constructing its
    // own, so the app keeps one HTTP connection pool and one TLS context,
    // not two. Owned by AuthManager and outlives every borrower.
    QNetworkAccessManager *networkAccessManager() const { return m_network; }

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

    // Outcome of refreshAfterRejection(). Kept apart on purpose: only
    // NotRecoverable means "sign in again"; the other failures leave the
    // stored refresh token — and so the session — untouched.
    enum class RefreshResult {
        Refreshed,      // a new access token is available: retry the request
        NotRecoverable, // no refresh token, it was rejected (invalid_grant, session now SignedOut),
                        // or a freshly minted token was still refused — refreshing again won't help
        Unavailable,    // couldn't reach the token endpoint — session presumed valid, retried later
        ServerError,    // the token endpoint answered with something else (5xx/429/invalid_client)
    };

    // For the API layer: Google answered 401 to a request sent with
    // `rejectedAccessToken`. That means the *access* token is unusable (most
    // often because the machine slept past its expiry while the monotonic
    // refresh timer stood still), not that the session is gone, so try to
    // mint a new one from the refresh token. Concurrent callers share one
    // token request; a caller whose token was already replaced returns
    // Refreshed immediately; a token that a forced refresh minted moments ago
    // and that is still refused is NotRecoverable rather than refreshed
    // again, so a persistent 401 cannot turn into a refresh loop. `done` is
    // skipped if `context` dies first.
    void refreshAfterRejection(const QString &rejectedAccessToken, QObject *context,
                               const std::function<void(RefreshResult)> &done);

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

    // Emitted (at most once per run) when a refresh-token write had to fall
    // back to the unencrypted on-disk store because no OS keyring was
    // reachable. The session still persists across restarts; the UI should
    // tell the user their credentials aren't encrypted at rest.
    void credentialStorageInsecure();

private:
    enum class RefreshContext {
        Restore,
        Proactive,
        Forced, // refreshAfterRejection(): the API layer got a 401
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
    void beginNewAuthEpoch();
    void finishRefreshWaiters(RefreshResult result);
    void saveRefreshToken(const QString &token);
    void deleteStoredRefreshToken();
    void revokeStoredRefreshTokenBestEffort();

    QNetworkReply *postForm(const QUrl &endpoint, const QUrlQuery &params);

    QNetworkAccessManager *m_network;
    RealKeychainBackend m_realKeychainBackend; // used unless a test injects its own; declared before m_keychain
    KeychainBackend *m_keychain;
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

    // Latches once credentialStorageInsecure() has been emitted, so the
    // warning fires at most once per run however many times the token is
    // rewritten (proactive refresh rotates it).
    bool m_insecureStorageReported = false;

    // True from the moment a refresh_token request is sent until its reply
    // is handled (or superseded by a new epoch). Lets a forced refresh join
    // an in-flight one instead of racing it.
    bool m_refreshInFlight = false;
    struct RefreshWaiter
    {
        QPointer<QObject> context;
        std::function<void(RefreshResult)> done;
    };
    QList<RefreshWaiter> m_refreshWaiters;
    // Wall-clock (ms since epoch) of the last access token issued. Wall
    // clock, not a monotonic timer, precisely so a suspend can't make a
    // stale token look fresh.
    qint64 m_accessTokenIssuedMs = 0;
    bool m_accessTokenFromForcedRefresh = false; // the current token came from refreshAfterRejection()

    QTimer m_proactiveRefreshTimer;
    QTimer m_signInTimeoutTimer;
};

#endif // AUTHMANAGER_H
