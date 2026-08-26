#include "authmanager.h"
#include "oauthloopbackserver.h"
#include "pkce.h"

#include <keychain.h>

#include <QDesktopServices>
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
// Without this, a server that accepts the connection but never responds
// (black-holed by a firewall/proxy, hung process) leaves a request pending
// forever with no error ever surfaced to the user.
constexpr int kNetworkTimeoutMs = 20000;
} // namespace

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_signInTimeoutTimer.setSingleShot(true);
    connect(&m_signInTimeoutTimer, &QTimer::timeout, this, [this] {
        cleanupLoopbackServer();
        setState(AuthState::SignedOut);
        emit errorOccurred(tr("Sign-in timed out."));
    });

    m_proactiveRefreshTimer.setSingleShot(true);
    connect(&m_proactiveRefreshTimer, &QTimer::timeout, this, [this] {
        refreshAccessToken(RefreshContext::Proactive);
    });
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
    setState(AuthState::Restoring);

    auto *job = new QKeychain::ReadPasswordJob(CredentialsProvider::keychainService, this);
    job->setKey(kRefreshTokenKey);
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job *job) {
        auto *readJob = qobject_cast<QKeychain::ReadPasswordJob *>(job);
        if (readJob->error() != QKeychain::NoError || readJob->textData().isEmpty()) {
            setState(AuthState::SignedOut);
            return;
        }

        m_refreshToken = readJob->textData();
        loadStoredRefreshTokenThenRestore();
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
                setState(AuthState::SignedIn);
                emit signedIn();
            }
            // Proactive refresh: state is already SignedIn, nothing else to do.
        },
        [this, context](const QString &) {
            m_refreshToken.clear();
            deleteStoredRefreshToken();
            setState(AuthState::SignedOut);
            // Restore context fails silently on a routine cold start; a
            // proactive refresh failing mid-session is a real, user-visible
            // event since it interrupts an active session.
            if (context == RefreshContext::Proactive)
                emit errorOccurred(tr("Your session expired. Please sign in again."));
        });
}

void AuthManager::signIn(QWidget *dialogParent)
{
    if (m_state != AuthState::SignedOut)
        return;

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

            setState(AuthState::SignedIn);
            emit signedIn();
        },
        [this](const QString &error) {
            setState(AuthState::SignedOut);
            emit errorOccurred(tr("Sign-in failed: %1").arg(error));
        });
}

void AuthManager::signOut()
{
    if (m_state == AuthState::SignedOut)
        return;

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
                                    const std::function<void(const QString &)> &onFailure)
{
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onFailure] {
        reply->deleteLater();

        const QByteArray body = reply->readAll();
        QString error;
        const std::optional<TokenResponse> response = TokenResponse::fromJson(body, &error);

        if (!response) {
            if (body.isEmpty())
                error = reply->errorString();
            onFailure(error);
            return;
        }

        onSuccess(*response);
    });
}
