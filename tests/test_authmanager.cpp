#include "auth/authmanager.h"

#include <QNetworkReply>
#include <QTest>

class TestAuthManager : public QObject
{
    Q_OBJECT
private slots:
    void connectivityErrorsWithoutStatusAreTransient();
    void invalidGrantBodyIsInvalidGrant();
    void serverAnsweredIsNeverTransient();
    void nonInvalidGrantErrorBodyIsServerError();
    void emptyOrMalformedBodyWithoutConnectivityErrorIsServerError();
    void keychainEntryNotFoundIsNoSession();
    void keychainBackendUnavailableIsRetriable();
    void keychainUserRefusalAndNoBackendAreFatal();
};

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

QTEST_APPLESS_MAIN(TestAuthManager)
#include "test_authmanager.moc"
