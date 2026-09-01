#include "authmanager.h"
#include "oauthloopbackserver.h"
#include "pkce.h"

#include <keychain.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {
const QUrl kAuthorizationEndpoint(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
const QUrl kTokenEndpoint(QStringLiteral("https://oauth2.googleapis.com/token"));
const QUrl kRevokeEndpoint(QStringLiteral("https://oauth2.googleapis.com/revoke"));
const QString kScope = QStringLiteral("https://www.googleapis.com/auth/calendar");
const QString kRefreshTokenKey = QStringLiteral("refresh_token");
constexpr int kSignInTimeoutMs = 5 * 60 * 1000;
constexpr qint64 kProactiveRefreshLeadMs = 60 * 1000;
// After a transient/server-side proactive-refresh failure the session is
// kept alive and the refresh is simply retried this soon, rather than
// tearing everything down.
constexpr int kRefreshRetryIntervalMs = 60 * 1000;
// A keychain read that fails because the backend isn't ready yet (autostart
// racing the KWallet daemon / D-Bus bus) is retried this many times, this
// far apart, before restore gives up.
constexpr int kKeychainReadMaxRetries = 3;
constexpr int kKeychainReadRetryDelayMs = 2000;
// Without this, a server that accepts the connection but never responds
// (black-holed by a firewall/proxy, hung process) leaves a request pending
// forever with no error ever surfaced to the user.
constexpr int kNetworkTimeoutMs = 20000;

// Connectivity-class QNetworkReply errors: the request never reached a
// server that could answer. Mirrors GoogleCalendarApi::isTransientNetworkError
// (kept local so tgc_auth stays independent of tgc_calendar).
bool isConnectivityError(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: // transfer timeout fires as this
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::BackgroundRequestNotAllowedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::UnknownProxyError:
        return true;
    default:
        return false;
    }
}
} // namespace

AuthManager::TokenFailure AuthManager::classifyTokenFailure(QNetworkReply::NetworkError error,
                                                            int httpStatusCode,
                                                            const QByteArray &body)
{
    // A response with a status code means the server answered — never a
    // connectivity problem, whatever the QNetworkReply code says.
    if (httpStatusCode < 400 && isConnectivityError(error))
        return TokenFailure::Transient;

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    if (obj.value(QStringLiteral("error")).toString() == QStringLiteral("invalid_grant"))
        return TokenFailure::InvalidGrant;

    return TokenFailure::ServerError;
}

AuthManager::KeychainReadOutcome AuthManager::classifyKeychainReadError(QKeychain::Error error)
{
    switch (error) {
    case QKeychain::EntryNotFound:
        return KeychainReadOutcome::NoSession;
    case QKeychain::AccessDeniedByUser:
    case QKeychain::NotImplemented:
        return KeychainReadOutcome::Fatal;
    default:
        // AccessDenied / NoBackendAvailable / OtherError: the credential
        // store is momentarily unavailable (KWallet daemon or the D-Bus
        // session bus not up yet at login autostart). Retry before giving
        // up — never treat this as "no session".
        return KeychainReadOutcome::Retriable;
    }
}

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_signInTimeoutTimer.setSingleShot(true);
    connect(&m_signInTimeoutTimer, &QTimer::timeout, this, [this] {
        cleanupLoopbackServer();
        m_lastSignOutReason = SignOutReason::SignInFailed;
        setState(AuthState::SignedOut);
        emit errorOccurred(tr("Sign-in timed out."));
    });

    m_proactiveRefreshTimer.setSingleShot(true);
    connect(&m_proactiveRefreshTimer, &QTimer::timeout, this, [this] {
        refreshAccessToken(RefreshContext::Proactive);
    });
}

QString AuthManager::accountKey() const
{
    if (m_refreshToken.isEmpty())
        return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(m_refreshToken.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QUrl AuthManager::buildAuthorizationUrl(const OAuthClientCredentials &credentials,
                                        quint16 loopbackPort,
                                        const QByteArray &codeChallenge,
                                        const QString &state)
{
    QUrl url = kAuthorizationEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), credentials.clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"),
                        QStringLiteral("http://127.0.0.1:%1/callback").arg(loopbackPort));
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), kScope);
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromUtf8(codeChallenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), state);
    url.setQuery(query);
    return url;
}

void AuthManager::setState(AuthState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void AuthManager::resolveCredentials(bool allowInteractiveFallback,
                                     QWidget *dialogParent,
                                     const std::function<void(const OAuthClientCredentials &)> &onResolved,
                                     const std::function<void(const QString &)> &onFailed)
{
    auto *provider = new CredentialsProvider(this);
    connect(provider, &CredentialsProvider::resolved, this, [provider, onResolved](const OAuthClientCredentials &creds) {
        provider->deleteLater();
        onResolved(creds);
    });
    connect(provider, &CredentialsProvider::failed, this, [provider, onFailed](const QString &reason) {
        provider->deleteLater();
        onFailed(reason);
    });
    provider->resolve(allowInteractiveFallback, dialogParent);
}

void AuthManager::restoreSession()
{
    ++m_authEpoch;
    m_keychainReadAttempts = 0;
    setState(AuthState::Restoring);
    readStoredRefreshToken();
}

void AuthManager::readStoredRefreshToken()
{
    auto *job = new QKeychain::ReadPasswordJob(CredentialsProvider::keychainService, this);
    job->setKey(kRefreshTokenKey);
    const quint64 epoch = m_authEpoch;
    connect(job, &QKeychain::Job::finished, this, [this, epoch](QKeychain::Job *job) {
        // A sign-out or a fresh auth attempt superseded this restore.
        if (epoch != m_authEpoch)
            return;

        auto *readJob = qobject_cast<QKeychain::ReadPasswordJob *>(job);
        const QKeychain::Error error = readJob->error();

        if (error == QKeychain::NoError) {
            if (readJob->textData().isEmpty()) {
                m_lastSignOutReason = SignOutReason::None; // nothing stored
                setState(AuthState::SignedOut);
                return;
            }
            m_refreshToken = readJob->textData();
            loadStoredRefreshTokenThenRestore();
            return;
        }

        switch (classifyKeychainReadError(error)) {
        case KeychainReadOutcome::NoSession:
            m_lastSignOutReason = SignOutReason::None;
            setState(AuthState::SignedOut);
            return;
        case KeychainReadOutcome::Fatal:
            setState(AuthState::SignedOut);
            emit errorOccurred(tr("Couldn't open the system credential store. Please sign in again."));
            return;
        case KeychainReadOutcome::Retriable:
            if (m_keychainReadAttempts < kKeychainReadMaxRetries) {
                ++m_keychainReadAttempts;
                QTimer::singleShot(kKeychainReadRetryDelayMs, this, [this, epoch] {
                    if (epoch == m_authEpoch && m_state == AuthState::Restoring)
                        readStoredRefreshToken();
                });
                return;
            }
            // Retries exhausted: the token is presumed still on disk, so
            // this is "couldn't read it now", not "no session" — the UI can
            // keep silently retrying restore in the background.
            m_lastSignOutReason = SignOutReason::NetworkUnavailable;
            setState(AuthState::SignedOut);
            emit errorOccurred(tr("The system credential store is unavailable. Please sign in again."));
            return;
        }
    });
    job->start();
}

void AuthManager::loadStoredRefreshTokenThenRestore()
{
    resolveCredentials(
        /*allowInteractiveFallback=*/false, nullptr,
        [this](const OAuthClientCredentials &creds) {
            m_credentials = creds;
            refreshAccessToken(RefreshContext::Restore);
        },
        [this](const QString &) {
            m_lastSignOutReason = SignOutReason::SessionExpired;
            setState(AuthState::SignedOut);
            emit errorOccurred(tr("Could not restore your session. Please sign in again."));
        });
}

void AuthManager::refreshAccessToken(RefreshContext context)
{
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    params.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    params.addQueryItem(QStringLiteral("client_id"), m_credentials.clientId);
    params.addQueryItem(QStringLiteral("client_secret"), m_credentials.clientSecret);

    QNetworkReply *reply = postForm(kTokenEndpoint, params);
    handleTokenReply(
        reply,
        [this, context](const TokenResponse &response) {
            m_accessToken = response.accessToken;
            m_accessTokenExpiryUtc = response.expiresAtUtc;
            if (!response.refreshToken.isEmpty()) {
                m_refreshToken = response.refreshToken;
                saveRefreshToken(m_refreshToken);
            }
            scheduleProactiveRefresh();

            if (context == RefreshContext::Restore) {
                m_lastSignOutReason = SignOutReason::None;
                setState(AuthState::SignedIn);
                emit signedIn();
            }
            // Proactive refresh: state is already SignedIn, nothing else to do.
        },
        [this, context](TokenFailure failure, const QString &) {
            if (failure == TokenFailure::InvalidGrant) {
                // The refresh token is genuinely dead (revoked, expired,
                // password change). Discard it — retrying is pointless.
                m_refreshToken.clear();
                deleteStoredRefreshToken();
                m_lastSignOutReason = SignOutReason::SessionExpired;
                setState(AuthState::SignedOut);
                // Restore fails silently on a routine cold start; a proactive
                // refresh failing mid-session interrupts an active session.
                if (context == RefreshContext::Proactive)
                    emit errorOccurred(tr("Your session expired. Please sign in again."));
                return;
            }

            // Transient / server-side failure: the stored refresh token is
            // still presumed valid and is NEVER deleted here — doing so would
            // turn a network blip into a forced browser re-login and defeat
            // offline startup.
            if (context == RefreshContext::Proactive) {
                // Keep the session; just try again shortly.
                m_proactiveRefreshTimer.start(kRefreshRetryIntervalMs);
            } else {
                // Restore: no access token available right now. Mark the gate
                // as network-blocked (the UI keeps retrying restore silently)
                // and leave the token on disk for next time.
                m_lastSignOutReason = SignOutReason::NetworkUnavailable;
                setState(AuthState::SignedOut);
                emit errorOccurred(tr("Couldn't reach Google to restore your session. "
                                      "Check your connection and try again."));
            }
        });
}

void AuthManager::signIn(QWidget *dialogParent)
{
    if (m_state != AuthState::SignedOut)
        return;

    ++m_authEpoch;
    // Any exit from the interactive flow that lands back on SignedOut is a
    // sign-in failure; the success path clears this before signedIn().
    m_lastSignOutReason = SignOutReason::SignInFailed;
    setState(AuthState::SigningIn);

    resolveCredentials(
        /*allowInteractiveFallback=*/true, dialogParent,
        [this](const OAuthClientCredentials &creds) {
            m_credentials = creds;
            beginSignInFlow();
        },
        [this](const QString &reason) {
            setState(AuthState::SignedOut);
            emit errorOccurred(reason);
        });
}

void AuthManager::beginSignInFlow()
{
    m_loopbackServer = new OAuthLoopbackServer(this);
    if (!m_loopbackServer->listen()) {
        cleanupLoopbackServer();
        setState(AuthState::SignedOut);
        emit errorOccurred(tr("Could not start the local sign-in server."));
        return;
    }

    const Pkce::Pair pkce = Pkce::generate();
    m_pkceVerifier = pkce.verifier;
    m_expectedState = QString::fromUtf8(Pkce::randomUrlSafeString(16));
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1/callback").arg(m_loopbackServer->port());

    connect(m_loopbackServer, &OAuthLoopbackServer::authorizationReceived, this,
            [this](const QString &code, const QString &state) {
                m_signInTimeoutTimer.stop();
                if (state != m_expectedState) {
                    cleanupLoopbackServer();
                    setState(AuthState::SignedOut);
                    emit errorOccurred(tr("Sign-in failed: unexpected response from Google."));
                    return;
                }
                cleanupLoopbackServer();
                exchangeAuthorizationCode(code);
            });
    connect(m_loopbackServer, &OAuthLoopbackServer::authorizationError, this,
            [this](const QString &error, const QString &) {
                m_signInTimeoutTimer.stop();
                cleanupLoopbackServer();
                setState(AuthState::SignedOut);
                const QString message = error == QStringLiteral("access_denied")
                    ? tr("Sign-in was cancelled.")
                    : tr("Sign-in failed: %1").arg(error);
                emit errorOccurred(message);
            });

    const QUrl authUrl = buildAuthorizationUrl(m_credentials, m_loopbackServer->port(),
                                                pkce.challenge, m_expectedState);
    QDesktopServices::openUrl(authUrl);
    m_signInTimeoutTimer.start(kSignInTimeoutMs);
}

void AuthManager::exchangeAuthorizationCode(const QString &code)
{
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    params.addQueryItem(QStringLiteral("code"), code);
    params.addQueryItem(QStringLiteral("client_id"), m_credentials.clientId);
    params.addQueryItem(QStringLiteral("client_secret"), m_credentials.clientSecret);
    params.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    params.addQueryItem(QStringLiteral("code_verifier"), QString::fromUtf8(m_pkceVerifier));

    QNetworkReply *reply = postForm(kTokenEndpoint, params);
    handleTokenReply(
        reply,
        [this](const TokenResponse &response) {
            if (response.refreshToken.isEmpty()) {
                setState(AuthState::SignedOut);
                emit errorOccurred(tr("Google did not grant offline access. Please try signing in again."));
                return;
            }

            m_accessToken = response.accessToken;
            m_accessTokenExpiryUtc = response.expiresAtUtc;
            m_refreshToken = response.refreshToken;
            saveRefreshToken(m_refreshToken);
            scheduleProactiveRefresh();

            m_lastSignOutReason = SignOutReason::None;
            setState(AuthState::SignedIn);
            emit signedIn();
        },
        [this](TokenFailure, const QString &error) {
            setState(AuthState::SignedOut);
            emit errorOccurred(tr("Sign-in failed: %1").arg(error));
        });
}

void AuthManager::signOut()
{
    if (m_state == AuthState::SignedOut)
        return;

    ++m_authEpoch;
    m_lastSignOutReason = SignOutReason::None;
    cleanupLoopbackServer();
    m_signInTimeoutTimer.stop();
    m_proactiveRefreshTimer.stop();

    revokeStoredRefreshTokenBestEffort();

    m_accessToken.clear();
    m_accessTokenExpiryUtc = QDateTime();
    m_refreshToken.clear();

    deleteStoredRefreshToken();

    setState(AuthState::SignedOut);
    emit signedOut();
}

void AuthManager::cleanupLoopbackServer()
{
    if (!m_loopbackServer)
        return;
    m_loopbackServer->close();
    m_loopbackServer->deleteLater();
    m_loopbackServer = nullptr;
}

void AuthManager::scheduleProactiveRefresh()
{
    const qint64 msUntilRefresh = QDateTime::currentDateTimeUtc().msecsTo(m_accessTokenExpiryUtc)
        - kProactiveRefreshLeadMs;
    m_proactiveRefreshTimer.start(static_cast<int>(qMax<qint64>(msUntilRefresh, 1000)));
}

void AuthManager::saveRefreshToken(const QString &token)
{
    auto *job = new QKeychain::WritePasswordJob(CredentialsProvider::keychainService, this);
    job->setKey(kRefreshTokenKey);
    job->setTextData(token);
    job->start();
}

void AuthManager::deleteStoredRefreshToken()
{
    auto *job = new QKeychain::DeletePasswordJob(CredentialsProvider::keychainService, this);
    job->setKey(kRefreshTokenKey);
    job->start();
}

void AuthManager::revokeStoredRefreshTokenBestEffort()
{
    if (m_refreshToken.isEmpty())
        return;

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("token"), m_refreshToken);
    QNetworkReply *reply = postForm(kRevokeEndpoint, params);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

QNetworkReply *AuthManager::postForm(const QUrl &endpoint, const QUrlQuery &params)
{
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(kNetworkTimeoutMs);
    return m_network->post(request, params.query(QUrl::FullyEncoded).toUtf8());
}

void AuthManager::handleTokenReply(QNetworkReply *reply,
                                    const std::function<void(const TokenResponse &)> &onSuccess,
                                    const std::function<void(TokenFailure, const QString &)> &onFailure)
{
    const quint64 epoch = m_authEpoch;
    connect(reply, &QNetworkReply::finished, this, [this, epoch, reply, onSuccess, onFailure] {
        reply->deleteLater();

        // A sign-out or a fresh auth attempt happened while this was in
        // flight: drop the reply entirely so it can't flip state back to
        // SignedIn or rewrite the keychain after sign-out.
        if (epoch != m_authEpoch)
            return;

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QString error;
        const std::optional<TokenResponse> response = TokenResponse::fromJson(body, &error);

        if (!response) {
            if (body.isEmpty())
                error = reply->errorString();
            onFailure(classifyTokenFailure(reply->error(), httpStatus, body), error);
            return;
        }

        onSuccess(*response);
    });
}
