#include "auth/authmanager.h"
#include "authtesthelpers.h"
#include "calendar/googlecalendarapi.h"
#include "fakekeychainbackend.h"
#include "fakenetworkaccessmanager.h"

#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using AuthTestHelpers::seedCredentialsConfigFile;
using AuthTestHelpers::tokenErrorJson;
using AuthTestHelpers::tokenResponseJson;

namespace {
const QString kRefreshTokenKey = QStringLiteral("refresh_token");

// A Google that mints a distinct access token per token-endpoint call and
// rejects (401) any token the test has declared expired — which is what
// Google does to a ~1 hour access token after the machine slept for hours.
// The token endpoint's behaviour for *later* refreshes is configurable, to
// separate "the access token expired" from "the refresh token / token
// endpoint is the problem".
struct FakeGoogle
{
    enum class TokenEndpoint {
        Ok,
        InvalidGrant, // refresh token revoked/expired: HTTP 400 {"error":"invalid_grant"}
        Unreachable,  // no HTTP response at all
        ServerError,  // HTTP 503
    };

    TokenEndpoint tokenEndpoint = TokenEndpoint::Ok;
    bool rejectAllTokens = false; // Calendar API refuses even brand-new tokens
    int mintCount = 0;
    QSet<QByteArray> expired; // "Bearer <token>" values Google now rejects

    static QByteArray bearer(int n) { return "Bearer access-" + QByteArray::number(n); }

    FakeNetworkReply::Response handle(const FakeNetworkAccessManager::RecordedRequest &request)
    {
        if (request.url.host() == QStringLiteral("oauth2.googleapis.com")) {
            if (request.url.path() != QStringLiteral("/token"))
                return {200, "{}"}; // /revoke
            switch (tokenEndpoint) {
            case TokenEndpoint::InvalidGrant:
                return {400, tokenErrorJson(QStringLiteral("invalid_grant"))};
            case TokenEndpoint::Unreachable:
                return {0, {}, QNetworkReply::HostNotFoundError, QStringLiteral("host not found")};
            case TokenEndpoint::ServerError:
                return {503, "upstream unavailable"};
            case TokenEndpoint::Ok:
                break;
            }
            ++mintCount;
            return {200, tokenResponseJson(QStringLiteral("access-%1").arg(mintCount), 3600)};
        }
        if (rejectAllTokens || expired.contains(request.authorization))
            return {401, R"({"error":{"code":401,"message":"Invalid Credentials"}})"};
        return {200, R"({"items":[{"id":"primary@example.com","summary":"Primary","accessRole":"owner"}]})"};
    }
};

int requestsTo(const FakeNetworkAccessManager &net, const QString &host, const QString &path = QString())
{
    int count = 0;
    for (const auto &request : net.requests) {
        if (request.url.host() == host && (path.isEmpty() || request.url.path() == path))
            ++count;
    }
    return count;
}
int tokenRequests(const FakeNetworkAccessManager &net)
{
    return requestsTo(net, QStringLiteral("oauth2.googleapis.com"), QStringLiteral("/token"));
}
int calendarRequests(const FakeNetworkAccessManager &net)
{
    return requestsTo(net, QStringLiteral("www.googleapis.com"));
}
} // namespace

// Regression tests for "PC wakes from a multi-hour suspend, calendae reports
// that the session may have expired and shows no new events, yet restarting
// the app works without signing in again".
//
// Root cause: AuthManager refreshes the ~1 h access token with a single-shot
// QTimer set to (expiry - 60 s). Qt timers run on CLOCK_MONOTONIC, which does
// not advance while the machine is suspended, whereas Google judges the
// token by the wall clock. After a suspend of S the timer therefore fires S
// late, leaving a window (up to ~1 h after wake) in which the app still holds
// — and sends — an access token Google has already expired. The refresh token
// itself is untouched, which is why a restart (restoreSession -> fresh access
// token) always healed everything.
//
// Fix under test: a 401 makes GoogleCalendarApi ask AuthManager for a new
// access token and re-send the request once. The tests below cannot suspend
// the machine and do not need to: the proactive timer simply never fires
// within the test (it is ~59 minutes away, exactly as it is right after a
// real wake), and the fake Google rejects the held token.
//
// The second half guards the failure modes of that recovery: an expired
// *access* token must be told apart from a dead *refresh* token, and neither
// an unrecoverable session nor a persistent 401 may turn into a refresh loop.
//
// IMPORTANT: every AuthManager here gets an explicit FakeKeychainBackend; see
// [[testing-never-touch-real-keychain]].
class TestWakeFromSuspend : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() { seedCredentialsConfigFile(); }

    // Recovery.
    void expiredAccessTokenIsRefreshedAndRequestRetried();
    void retriedCreateEventResendsSameBodyWithNewToken();
    void concurrentRejectionsShareOneRefresh();
    void restartMintsWorkingAccessTokenFromSameRefreshToken(); // control

    // Refresh token / token endpoint problems: no retry loops, session kept
    // or ended for the right reason.
    void deadRefreshTokenEndsSessionAfterOneAttempt();
    void unreachableTokenEndpointKeepsSessionAndIsTransient();
    void tokenEndpointServerErrorKeepsSessionAndIsNotReportedAsExpired();
    void freshTokenStillRefusedDoesNotLoop();
    void signOutWhileRefreshingAnswersTheRequest();
};

namespace {
// Boilerplate shared by every test: a signed-in AuthManager (access-1) whose
// access token Google has since expired ("hours of suspend"), with the
// proactive-refresh timer still ~59 min away.
struct SuspendedSession
{
    FakeGoogle google;
    FakeNetworkAccessManager net;
    FakeKeychainBackend keychain;
    AuthManager auth;
    GoogleCalendarApi api;
    QSignalSpy fetched;
    QSignalSpy failed;

    SuspendedSession()
        : auth(nullptr, &net, &keychain)
        , api(&auth)
        , fetched(&api, &GoogleCalendarApi::calendarListFetched)
        , failed(&api, &GoogleCalendarApi::calendarListFetchFailed)
    {
        keychain.entries[kRefreshTokenKey] = QStringLiteral("long-lived-refresh-token");
        net.handler = [this](const FakeNetworkAccessManager::RecordedRequest &r) { return google.handle(r); };
    }

    // Not in the constructor: QTest's verification macros only abort the
    // enclosing function, so callers check QTest::currentTestFailed().
    void start()
    {
        auth.restoreSession();
        QTRY_COMPARE_WITH_TIMEOUT(auth.state(), AuthManager::AuthState::SignedIn, 5000);
        QCOMPARE(auth.accessToken(), QStringLiteral("access-1"));
        google.expired.insert(FakeGoogle::bearer(1));
    }
};
} // namespace

void TestWakeFromSuspend::expiredAccessTokenIsRefreshedAndRequestRetried()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(tokenRequests(s.net), 1);

    s.api.fetchCalendarList();
    QTRY_VERIFY(s.fetched.count() + s.failed.count() > 0);

    // Previously: calendarListFetchFailed("Your session may have expired...").
    QCOMPARE(s.failed.count(), 0);
    QCOMPARE(s.fetched.count(), 1);
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(s.auth.accessToken(), QStringLiteral("access-2"));
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 2);
    QCOMPARE(s.net.requests.last().authorization, FakeGoogle::bearer(2));
    QVERIFY(s.keychain.entries.contains(kRefreshTokenKey));
}

void TestWakeFromSuspend::retriedCreateEventResendsSameBodyWithNewToken()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    QSignalSpy createdSpy(&s.api, &GoogleCalendarApi::eventCreated);
    QSignalSpy createFailedSpy(&s.api, &GoogleCalendarApi::eventCreateFailed);

    NewEventRequest request;
    request.calendarId = QStringLiteral("primary@example.com");
    request.summary = QStringLiteral("Dentist");
    request.allDay = true;
    request.startDate = QDate(2026, 9, 21);
    request.endDateExclusive = QDate(2026, 9, 22);
    s.api.createEvent(7, request);

    QTRY_VERIFY(createdSpy.count() + createFailedSpy.count() > 0);
    QCOMPARE(createFailedSpy.count(), 0);
    QCOMPARE(createdSpy.first().at(0).toULongLong(), quint64(7));

    QList<FakeNetworkAccessManager::RecordedRequest> posts;
    for (const auto &r : s.net.requests) {
        if (r.operation == QNetworkAccessManager::PostOperation && r.url.host() == QStringLiteral("www.googleapis.com"))
            posts.append(r);
    }
    QCOMPARE(posts.size(), 2); // rejected once, sent once more — never a third time
    QVERIFY(!posts.first().body.isEmpty());
    QCOMPARE(posts.last().body, posts.first().body);
    QCOMPARE(posts.first().authorization, FakeGoogle::bearer(1));
    QCOMPARE(posts.last().authorization, FakeGoogle::bearer(2));
}

void TestWakeFromSuspend::concurrentRejectionsShareOneRefresh()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;

    // Several views/controllers + the reminder scheduler all hit the stale
    // token at once after a wake.
    s.api.fetchCalendarList();
    s.api.fetchCalendarList();
    s.api.fetchCalendarList();
    QTRY_COMPARE(s.fetched.count() + s.failed.count(), 3);

    QCOMPARE(s.failed.count(), 0);
    QCOMPARE(tokenRequests(s.net), 2); // the restore, plus exactly one refresh for all three
    QCOMPARE(calendarRequests(s.net), 6);
}

void TestWakeFromSuspend::restartMintsWorkingAccessTokenFromSameRefreshToken()
{
    FakeGoogle google;
    FakeNetworkAccessManager net;
    net.handler = [&google](const FakeNetworkAccessManager::RecordedRequest &r) { return google.handle(r); };
    FakeKeychainBackend keychain;
    keychain.entries[kRefreshTokenKey] = QStringLiteral("long-lived-refresh-token");

    {
        AuthManager before(nullptr, &net, &keychain);
        before.restoreSession();
        QTRY_COMPARE(before.state(), AuthManager::AuthState::SignedIn);
    }
    google.expired.insert(FakeGoogle::bearer(1)); // hours of suspend later

    // "Close the app and open it again": a brand new AuthManager on the same
    // keychain, no interactive sign-in involved.
    AuthManager after(nullptr, &net, &keychain);
    GoogleCalendarApi api(&after);
    QSignalSpy fetchedSpy(&api, &GoogleCalendarApi::calendarListFetched);
    QSignalSpy failedSpy(&api, &GoogleCalendarApi::calendarListFetchFailed);

    after.restoreSession();
    QTRY_COMPARE(after.state(), AuthManager::AuthState::SignedIn);
    QCOMPARE(after.accessToken(), QStringLiteral("access-2"));

    api.fetchCalendarList();
    QTRY_COMPARE(fetchedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
}

void TestWakeFromSuspend::deadRefreshTokenEndsSessionAfterOneAttempt()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    s.google.tokenEndpoint = FakeGoogle::TokenEndpoint::InvalidGrant;

    s.api.fetchCalendarList();
    QTRY_COMPARE(s.failed.count(), 1);

    // The refresh token itself was refused: that, and only that, is a dead
    // session — discarded, signed out, interactive sign-in required.
    QVERIFY(s.failed.first().at(0).toString().contains(QStringLiteral("sign in again")));
    QCOMPARE(s.failed.first().at(1).toBool(), false);
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(s.auth.lastSignOutReason(), AuthManager::SignOutReason::SessionExpired);
    QTRY_VERIFY(!s.keychain.entries.contains(kRefreshTokenKey));

    // One refresh attempt, the request itself not repeated, and nothing
    // keeps trying afterwards.
    QTest::qWait(300);
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 1);
    s.api.fetchCalendarList(); // signed out now: refused locally, no traffic
    QCOMPARE(s.failed.count(), 2);
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 1);
}

void TestWakeFromSuspend::unreachableTokenEndpointKeepsSessionAndIsTransient()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    s.google.tokenEndpoint = FakeGoogle::TokenEndpoint::Unreachable;

    s.api.fetchCalendarList();
    QTRY_COMPARE(s.failed.count(), 1);

    // Couldn't reach Google to renew: says so (transient => the UI's offline
    // state and quiet retry), and never mistakes it for a dead session.
    QCOMPARE(s.failed.first().at(1).toBool(), true);
    QVERIFY(!s.failed.first().at(0).toString().contains(QStringLiteral("sign in again")));
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedIn);
    QVERIFY(s.keychain.entries.contains(kRefreshTokenKey));
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 1);
}

void TestWakeFromSuspend::tokenEndpointServerErrorKeepsSessionAndIsNotReportedAsExpired()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    s.google.tokenEndpoint = FakeGoogle::TokenEndpoint::ServerError;

    s.api.fetchCalendarList();
    QTRY_COMPARE(s.failed.count(), 1);

    QVERIFY(!s.failed.first().at(0).toString().contains(QStringLiteral("sign in again")));
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedIn);
    QVERIFY(s.keychain.entries.contains(kRefreshTokenKey));
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 1);
}

void TestWakeFromSuspend::freshTokenStillRefusedDoesNotLoop()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    s.google.rejectAllTokens = true; // not an expiry problem: no token works

    s.api.fetchCalendarList();
    QTRY_COMPARE(s.failed.count(), 1);

    // Exactly one refresh and one retry, then the 401 is reported as is.
    QVERIFY(s.failed.first().at(0).toString().contains(QStringLiteral("sign in again")));
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedIn); // not destroyed on a hunch
    QVERIFY(s.keychain.entries.contains(kRefreshTokenKey));
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 2);

    // The next request right after: goes out once, is refused, and does NOT
    // trigger yet another refresh (the token was just forced-minted).
    s.api.fetchCalendarList();
    QTRY_COMPARE(s.failed.count(), 2);
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 3);

    QTest::qWait(300); // and nothing retries by itself
    QCOMPARE(tokenRequests(s.net), 2);
    QCOMPARE(calendarRequests(s.net), 3);
}

void TestWakeFromSuspend::signOutWhileRefreshingAnswersTheRequest()
{
    SuspendedSession s;
    s.start();
    if (QTest::currentTestFailed())
        return;
    // Sign out after the 401 has been seen and the token request is out, but
    // before its reply lands (that reply's own zero-timer is queued after this
    // one). The request must still be answered, and nothing may be left hung.
    auto baseHandler = s.net.handler;
    s.net.handler = [&s, baseHandler](const FakeNetworkAccessManager::RecordedRequest &r) {
        if (r.url.host() == QStringLiteral("oauth2.googleapis.com") && r.url.path() == QStringLiteral("/token"))
            QTimer::singleShot(0, &s.auth, [&s] { s.auth.signOut(); });
        return baseHandler(r);
    };

    s.api.fetchCalendarList();
    QTRY_COMPARE(s.failed.count(), 1);
    QCOMPARE(s.fetched.count(), 0);
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedOut);

    QTest::qWait(200); // the dropped token reply must not resurrect the session
    QCOMPARE(s.auth.state(), AuthManager::AuthState::SignedOut);
    QCOMPARE(s.failed.count(), 1);
}

QTEST_GUILESS_MAIN(TestWakeFromSuspend)
#include "test_wakefromsuspend.moc"
