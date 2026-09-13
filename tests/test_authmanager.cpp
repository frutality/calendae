#include "auth/authmanager.h"
#include "authtesthelpers.h"
#include "fakekeychainbackend.h"
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

#include <algorithm>

using AuthTestHelpers::seedCredentialsConfigFile;
using AuthTestHelpers::tokenErrorJson;
using AuthTestHelpers::tokenResponseJson;

namespace {
const QString kTokenEndpoint = QStringLiteral("https://oauth2.googleapis.com/token");
const QString kRevokeEndpoint = QStringLiteral("https://oauth2.googleapis.com/revoke");
const QString kRefreshTokenKey = QStringLiteral("refresh_token");
} // namespace

// IMPORTANT: every AuthManager constructed here must be given an explicit
// FakeKeychainBackend (never leave the third constructor argument
// defaulted to nullptr) — the default constructs a RealKeychainBackend,
// which talks to the actual OS keyring under the exact service name
// ("calendae") the real installed app uses. That happened once by accident
// before this fake existed and deleted a developer's real stored Google
// session; see [[testing-never-touch-real-keychain]] in project memory.
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

    // restoreSession(), now safely exercisable end-to-end via the fake
    // keychain backend.
    void restoreSessionWithNoStoredTokenGoesStraightToSignedOut();
    void restoreSessionSucceedsAndSignsIn();
    void restoreSessionInvalidGrantDeletesStoredToken();
    void restoreSessionTransientNetworkFailureKeepsTokenOnDisk();
    void restoreSessionRetriableKeychainErrorEventuallyGivesUp();

    // Proactive refresh, triggered once already SignedIn.
    void proactiveRefreshRefreshesTokenWithoutChangingState();
    void proactiveRefreshInvalidGrantSignsOut();

    // Full interactive sign-in loopback flow. QDesktopServices::openUrl is
    // intercepted via setUrlHandler so no real browser is ever spawned.
    void signInHappyPathSavesTokenAndSignsIn();
    void signInStateMismatchFails();
    void signInAuthorizationErrorIsCancelled();
    void signInMissingRefreshTokenFailsWithoutSavingAnything();
    void signInIgnoredWhenAlreadyInProgress();

    void signOutRevokesAndDeletesStoredTokenThenIsIdempotent();

    void setSignedInForTestingSetsStateAndToken();
    void accountKeyIsEmptyUntilSignedIn();

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

void TestAuthManager::restoreSessionWithNoStoredTokenGoesStraightToSignedOut()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain; // empty: nothing stored
    AuthManager auth(nullptr, &net, &keychain);

    auth.restoreSession();

    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::None);
    QCOMPARE(net.requests.count(), 0); // never even reached the network
}

void TestAuthManager::restoreSessionSucceedsAndSignsIn()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("stored-refresh-token");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, tokenResponseJson(QStringLiteral("access-token-1"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
    QSignalSpy signedInSpy(&auth, &AuthManager::signedIn);

    auth.restoreSession();

    QVERIFY(signedInSpy.wait());
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(auth.accessToken(), QStringLiteral("access-token-1"));
    QCOMPARE(net.requests.count(), 1);
    QCOMPARE(net.requests.first().url.toString(), kTokenEndpoint);
    QVERIFY(net.requests.first().body.contains("grant_type=refresh_token"));
}

void TestAuthManager::restoreSessionInvalidGrantDeletesStoredToken()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("dead-refresh-token");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{400, tokenErrorJson(QStringLiteral("invalid_grant"), QStringLiteral("expired"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
    QSignalSpy errorSpy(&auth, &AuthManager::errorOccurred);

    auth.restoreSession();

    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::SessionExpired);
    QCOMPARE(errorSpy.count(), 0); // restore fails silently on a routine cold start
    QTRY_VERIFY(!keychain.entries.contains(kRefreshTokenKey));
}

void TestAuthManager::restoreSessionTransientNetworkFailureKeepsTokenOnDisk()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("still-good-token");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{0, QByteArray(), QNetworkReply::ConnectionRefusedError,
                                           QStringLiteral("Connection refused")};
    };

    AuthManager auth(nullptr, &net, &keychain);
    auth.restoreSession();

    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::NetworkUnavailable);
    QVERIFY(keychain.entries.contains(kRefreshTokenKey)); // never deleted on a mere network blip
}

void TestAuthManager::restoreSessionRetriableKeychainErrorEventuallyGivesUp()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    // Every read reports the backend as momentarily unavailable — never
    // "not found", never successful — as if KWallet/D-Bus hadn't come up
    // yet at login autostart. (No entry is seeded: FakeKeychainBackend
    // reports errorToReturnWhenMissing for any key it doesn't have.)
    keychain.errorToReturnWhenMissing = QKeychain::NoBackendAvailable;

    AuthManager auth(nullptr, &net, &keychain);
    QSignalSpy errorSpy(&auth, &AuthManager::errorOccurred);

    auth.restoreSession();

    // 3 retries at 2s apart (see kKeychainReadMaxRetries/kKeychainReadRetryDelayMs).
    QTRY_COMPARE_WITH_TIMEOUT(auth.state(), AuthManager::AuthState::SignedOut, 10000);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::CredentialStoreUnavailable);
    QCOMPARE(errorSpy.count(), 1);
}

void TestAuthManager::proactiveRefreshRefreshesTokenWithoutChangingState()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("token-for-proactive-refresh");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        static int callCount = 0;
        ++callCount;
        // expires_in well under the 60s proactive-refresh lead time clamps
        // the timer to its 1s floor, so the test doesn't need to wait long.
        return FakeNetworkReply::Response{200, tokenResponseJson(
            callCount == 1 ? QStringLiteral("access-token-first") : QStringLiteral("access-token-refreshed"),
            30)};
    };

    AuthManager auth(nullptr, &net, &keychain);
    auth.restoreSession();

    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(auth.accessToken(), QStringLiteral("access-token-first"));

    QTRY_COMPARE(auth.accessToken(), QStringLiteral("access-token-refreshed"));
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedIn); // proactive refresh never changes state
    QCOMPARE(net.requests.count(), 2);
}

void TestAuthManager::proactiveRefreshInvalidGrantSignsOut()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("token-that-gets-revoked");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        static int callCount = 0;
        ++callCount;
        if (callCount == 1)
            return FakeNetworkReply::Response{200, tokenResponseJson(QStringLiteral("access-token-first"), 30)};
        return FakeNetworkReply::Response{400, tokenErrorJson(QStringLiteral("invalid_grant"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
    QSignalSpy errorSpy(&auth, &AuthManager::errorOccurred);

    auth.restoreSession();
    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedIn);

    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::SessionExpired);
    // A proactive refresh failing mid-session interrupts an active session,
    // unlike a silent restore failure.
    QCOMPARE(errorSpy.count(), 1);
    QTRY_VERIFY(!keychain.entries.contains(kRefreshTokenKey));
}

void TestAuthManager::signInHappyPathSavesTokenAndSignsIn()
{
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "onAuthUrlOpened");

    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{
            200, tokenResponseJson(QStringLiteral("access-token-signin"), 3600, QStringLiteral("fresh-refresh-token"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
    QSignalSpy signedInSpy(&auth, &AuthManager::signedIn);

    auth.signIn(nullptr);

    QTRY_VERIFY(!m_capturedAuthUrl.isEmpty());
    QCOMPARE(auth.state(), AuthManager::AuthState::SigningIn);

    const QUrlQuery query(m_capturedAuthUrl);
    const QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    QVERIFY(redirectUri.port() > 0);

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(redirectUri.port()));
    QVERIFY(socket.waitForConnected());
    socket.write(QStringLiteral("GET /callback?code=test-auth-code&state=%1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
                     .arg(state)
                     .toUtf8());
    QVERIFY(socket.waitForBytesWritten());

    QVERIFY(signedInSpy.wait());
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(auth.accessToken(), QStringLiteral("access-token-signin"));
    QTRY_COMPARE(keychain.entries.value(kRefreshTokenKey), QStringLiteral("fresh-refresh-token"));

    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestAuthManager::signInStateMismatchFails()
{
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "onAuthUrlOpened");

    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    AuthManager auth(nullptr, &net, &keychain);
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
    FakeKeychainBackend keychain;
    AuthManager auth(nullptr, &net, &keychain);
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
    FakeKeychainBackend keychain;
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        // No refresh_token: Google didn't grant offline access.
        return FakeNetworkReply::Response{200, tokenResponseJson(QStringLiteral("access-token-only"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
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
    QVERIFY(keychain.entries.isEmpty());

    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestAuthManager::signInIgnoredWhenAlreadyInProgress()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    AuthManager auth(nullptr, &net, &keychain);
    QSignalSpy stateSpy(&auth, &AuthManager::stateChanged);

    auth.signIn(nullptr);
    auth.signIn(nullptr); // must be a no-op: state is already SigningIn

    QCOMPARE(stateSpy.count(), 1);
}

void TestAuthManager::signOutRevokesAndDeletesStoredTokenThenIsIdempotent()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("token-to-revoke");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, tokenResponseJson(QStringLiteral("access-token"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
    auth.restoreSession();
    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedIn);
    net.requests.clear(); // only care about requests made after sign-out

    QSignalSpy signedOutSpy(&auth, &AuthManager::signedOut);
    auth.signOut();

    QCOMPARE(signedOutSpy.count(), 1); // fires synchronously, unlike the async keychain delete below
    QCOMPARE(auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(auth.lastSignOutReason(), AuthManager::SignOutReason::None);
    QVERIFY(auth.accessToken().isEmpty());

    const auto revokeRequests = std::count_if(net.requests.begin(), net.requests.end(), [](const auto &r) {
        return r.url.toString() == kRevokeEndpoint;
    });
    QCOMPARE(revokeRequests, 1);
    QTRY_VERIFY(!keychain.entries.contains(kRefreshTokenKey));

    // Calling signOut() again while already signed out must do nothing.
    const int requestsBefore = net.requests.count();
    auth.signOut();
    QCOMPARE(signedOutSpy.count(), 1);
    QCOMPARE(net.requests.count(), requestsBefore);
}

void TestAuthManager::setSignedInForTestingSetsStateAndToken()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    AuthManager auth(nullptr, &net, &keychain);

    auth.setSignedInForTesting(QStringLiteral("a-token"));

    QCOMPARE(auth.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(auth.accessToken(), QStringLiteral("a-token"));
    QCOMPARE(net.requests.count(), 0); // touches neither network nor keychain
    QVERIFY(keychain.entries.isEmpty());
}

void TestAuthManager::accountKeyIsEmptyUntilSignedIn()
{
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("some-refresh-token");
    net.handler = [](const FakeNetworkAccessManager::RecordedRequest &) {
        return FakeNetworkReply::Response{200, tokenResponseJson(QStringLiteral("access-token"))};
    };

    AuthManager auth(nullptr, &net, &keychain);
    QCOMPARE(auth.accountKey(), QString());

    auth.restoreSession();
    QTRY_COMPARE(auth.state(), AuthManager::AuthState::SignedIn);

    QCOMPARE(auth.accountKey().size(), 64); // hex SHA-256
}

// QTEST_MAIN (not QTEST_GUILESS_MAIN): QDesktopServices::openUrl() silently
// no-ops without a QGuiApplication, which the sign-in tests rely on to
// intercept the authorization URL via setUrlHandler().
QTEST_MAIN(TestAuthManager)
#include "test_authmanager.moc"
