#include "auth/authmanager.h"
#include "authtesthelpers.h"
#include "fakenetworkaccessmanager.h"

#include <QDesktopServices>
#include <QHostAddress>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

using AuthTestHelpers::seedCredentialsConfigFile;
using AuthTestHelpers::tokenResponseJson;

// IMPORTANT: no test in this file may call restoreSession() or reach
// signIn()'s success path — both do real QtKeychain I/O against
// CredentialsProvider::keychainService ("calendae"), the exact service name
// the real app uses. On a machine with a real OS keyring available,
// QtKeychain's insecureFallback is NOT consulted for an ordinary read/write/
// delete, so exercising those paths here would touch — and could destroy —
// a developer's real stored Google session. (This actually happened once;
// see git history / PR discussion.) Everything below either fails before
// touching the keychain (state mismatch, auth error, missing refresh token,
// the not-signed-in reentrancy guard) or uses
// AuthManager::setSignedInForTesting() instead of a real sign-in.
class TestAuthManager : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();

    // Pure classification helpers (no I/O).
    void connectivityErrorsWithoutStatusAreTransient();
    void invalidGrantBodyIsInvalidGrant();
    void serverAnsweredIsNeverTransient();
    void nonInvalidGrantErrorBodyIsServerError();
    void emptyOrMalformedBodyWithoutConnectivityErrorIsServerError();
    void keychainEntryNotFoundIsNoSession();
    void keychainBackendUnavailableIsRetriable();
    void keychainUserRefusalAndNoBackendAreFatal();

    // Interactive sign-in loopback flow, up to (but never past) the point
    // where a real token would be saved. QDesktopServices::openUrl is
    // intercepted via setUrlHandler so no real browser is ever spawned.
    void signInStateMismatchFails();
    void signInAuthorizationErrorIsCancelled();
    void signInMissingRefreshTokenFailsWithoutSavingAnything();
    void signInIgnoredWhenAlreadyInProgress();

    void setSignedInForTestingSetsStateAndToken();

    // Not auto-run as a test: QTest only invokes zero-argument private
    // slots. Must still be a real slot (not a plain method) so
    // QDesktopServices::setUrlHandler's QMetaObject::invokeMethod-by-name
    // dispatch can actually find and call it.
    void onAuthUrlOpened(const QUrl &url) { m_capturedAuthUrl = url; }

private:
    QUrl m_capturedAuthUrl;
};

void TestAuthManager::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestAuthManager::init()
{
    seedCredentialsConfigFile();
    m_capturedAuthUrl.clear();
}

void TestAuthManager::connectivityErrorsWithoutStatusAreTransient()
{
    for (auto error : {QNetworkReply::ConnectionRefusedError,
                       QNetworkReply::HostNotFoundError,
                       QNetworkReply::TimeoutError,
                       QNetworkReply::OperationCanceledError, // transfer-timeout
                       QNetworkReply::TemporaryNetworkFailureError,
                       QNetworkReply::ProxyConnectionRefusedError,
                       QNetworkReply::UnknownNetworkError}) {
        QCOMPARE(AuthManager::classifyTokenFailure(error, 0, QByteArray()),
                 AuthManager::TokenFailure::Transient);
    }
}

void TestAuthManager::invalidGrantBodyIsInvalidGrant()
{
    const QByteArray body = R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})";
    // Google returns HTTP 400 for a dead refresh token.
    QCOMPARE(AuthManager::classifyTokenFailure(QNetworkReply::ProtocolInvalidOperationError, 400, body),
             AuthManager::TokenFailure::InvalidGrant);
}

void TestAuthManager::serverAnsweredIsNeverTransient()
{
    // A connectivity-looking QNetworkReply code paired with a real HTTP
    // status must not be treated as transient — the server did answer.
    QCOMPARE(AuthManager::classifyTokenFailure(QNetworkReply::ConnectionRefusedError, 503, QByteArray()),
             AuthManager::TokenFailure::ServerError);
    QCOMPARE(AuthManager::classifyTokenFailure(QNetworkReply::UnknownNetworkError, 500, "upstream boom"),
             AuthManager::TokenFailure::ServerError);
}

void TestAuthManager::nonInvalidGrantErrorBodyIsServerError()
{
    // Bad client credentials must NOT be mistaken for a dead refresh token:
    // the token may be fine, so it must survive.
    const QByteArray body = R"({"error":"invalid_client","error_description":"The OAuth client was not found."})";
    QCOMPARE(AuthManager::classifyTokenFailure(QNetworkReply::AuthenticationRequiredError, 401, body),
             AuthManager::TokenFailure::ServerError);
}

void TestAuthManager::emptyOrMalformedBodyWithoutConnectivityErrorIsServerError()
{
    QCOMPARE(AuthManager::classifyTokenFailure(QNetworkReply::ProtocolFailure, 0, QByteArray()),
             AuthManager::TokenFailure::ServerError);
    QCOMPARE(AuthManager::classifyTokenFailure(QNetworkReply::NoError, 0, "<html>gateway</html>"),
             AuthManager::TokenFailure::ServerError);
}

void TestAuthManager::keychainEntryNotFoundIsNoSession()
{
    QCOMPARE(AuthManager::classifyKeychainReadError(QKeychain::EntryNotFound),
             AuthManager::KeychainReadOutcome::NoSession);
}

void TestAuthManager::keychainBackendUnavailableIsRetriable()
{
    // Autostart racing the KWallet daemon / D-Bus bus surfaces as one of
    // these — a valid stored session must survive it, not be dropped.
    for (auto error : {QKeychain::NoBackendAvailable,
                       QKeychain::AccessDenied,
                       QKeychain::OtherError}) {
        QCOMPARE(AuthManager::classifyKeychainReadError(error),
                 AuthManager::KeychainReadOutcome::Retriable);
    }
}

void TestAuthManager::keychainUserRefusalAndNoBackendAreFatal()
{
    QCOMPARE(AuthManager::classifyKeychainReadError(QKeychain::AccessDeniedByUser),
             AuthManager::KeychainReadOutcome::Fatal);
    QCOMPARE(AuthManager::classifyKeychainReadError(QKeychain::NotImplemented),
             AuthManager::KeychainReadOutcome::Fatal);
}

void TestAuthManager::signInStateMismatchFails()
{
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "onAuthUrlOpened");

    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    QSignalSpy errorSpy(&auth, &AuthManager::errorOccurred);

    auth.signIn(nullptr);
    QTRY_VERIFY(!m_capturedAuthUrl.isEmpty());

    const QUrlQuery query(m_capturedAuthUrl);
    const QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(redirectUri.port()));
    QVERIFY(socket.waitForConnected());
    socket.write("GET /callback?code=test-auth-code&state=totally-wrong HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    QVERIFY(socket.waitForBytesWritten());

    QVERIFY(errorSpy.wait());
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::SignInFailed);
    QCOMPARE(net.requests.count(), 0); // never even reached the token endpoint

    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestAuthManager::signInAuthorizationErrorIsCancelled()
{
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "onAuthUrlOpened");

    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    QSignalSpy errorSpy(&auth, &AuthManager::errorOccurred);

    auth.signIn(nullptr);
    QTRY_VERIFY(!m_capturedAuthUrl.isEmpty());

    const QUrlQuery query(m_capturedAuthUrl);
    const QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    const QString state = query.queryItemValue(QStringLiteral("state"));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(redirectUri.port()));
    QVERIFY(socket.waitForConnected());
    socket.write(QStringLiteral("GET /callback?error=access_denied&state=%1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
                     .arg(state)
                     .toUtf8());
    QVERIFY(socket.waitForBytesWritten());

    QVERIFY(errorSpy.wait());
    QCOMPARE(errorSpy.first().first().toString(), QStringLiteral("Sign-in was cancelled."));
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedOut);

    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestAuthManager::signInMissingRefreshTokenFailsWithoutSavingAnything()
{
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "onAuthUrlOpened");

    FakeNetworkAccessManager net;
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        // No refresh_token: Google didn't grant offline access. This is the
        // one token-exchange success response that must NOT reach
        // saveRefreshToken() — the assertions below would otherwise be
        // exercising real keychain I/O.
        return FakeNetworkReply::Response{200, tokenResponseJson(QStringLiteral("access-token-only"))};
    };

    AuthManager auth(nullptr, &net);
    QSignalSpy errorSpy(&auth, &AuthManager::errorOccurred);

    auth.signIn(nullptr);
    QTRY_VERIFY(!m_capturedAuthUrl.isEmpty());

    const QUrlQuery query(m_capturedAuthUrl);
    const QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    const QString state = query.queryItemValue(QStringLiteral("state"));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(redirectUri.port()));
    QVERIFY(socket.waitForConnected());
    socket.write(QStringLiteral("GET /callback?code=test-auth-code&state=%1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
                     .arg(state)
                     .toUtf8());
    QVERIFY(socket.waitForBytesWritten());

    QVERIFY(errorSpy.wait());
    QCOMPARE(errorSpy.first().first().toString(),
             QStringLiteral("Google did not grant offline access. Please try signing in again."));
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedOut);

    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestAuthManager::signInIgnoredWhenAlreadyInProgress()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);
    QSignalSpy stateSpy(&auth, &AuthManager::stateChanged);

    auth.signIn(nullptr);
    auth.signIn(nullptr); // must be a no-op: state is already SigningIn

    QCOMPARE(stateSpy.count(), 1);
}

void TestAuthManager::setSignedInForTestingSetsStateAndToken()
{
    FakeNetworkAccessManager net;
    AuthManager auth(nullptr, &net);

    auth.setSignedInForTesting(QStringLiteral("a-token"));

    QCOMPARE(auth.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(auth.accessToken(), QStringLiteral("a-token"));
    QCOMPARE(net.requests.count(), 0); // touches neither network nor keychain
}

// QTEST_MAIN (not QTEST_GUILESS_MAIN): QDesktopServices::openUrl() silently
// no-ops without a QGuiApplication, which the sign-in tests rely on to
// intercept the authorization URL via setUrlHandler().
QTEST_MAIN(TestAuthManager)
#include "test_authmanager.moc"
