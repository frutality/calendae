#include "auth/tokenresponse.h"

#include <QTest>

class TestTokenResponse : public QObject
{
    Q_OBJECT
private slots:
    void parsesSuccessResponse();
    void parsesErrorResponse();
    void rejectsMalformedJson();
};

void TestTokenResponse::parsesSuccessResponse()
{
    const QByteArray json = R"({
        "access_token": "abc123",
        "refresh_token": "refresh456",
        "expires_in": 3600,
        "scope": "https://www.googleapis.com/auth/calendar",
        "token_type": "Bearer"
    })";

    QString error;
    const auto response = TokenResponse::fromJson(json, &error);
    QVERIFY(response.has_value());
    QCOMPARE(response->accessToken, QStringLiteral("abc123"));
    QCOMPARE(response->refreshToken, QStringLiteral("refresh456"));
    QVERIFY(response->expiresAtUtc > QDateTime::currentDateTimeUtc());
    QVERIFY(error.isEmpty());
}

void TestTokenResponse::parsesErrorResponse()
{
    const QByteArray json = R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})";
    QString error;
    const auto response = TokenResponse::fromJson(json, &error);
    QVERIFY(!response.has_value());
    QVERIFY(error.contains(QStringLiteral("invalid_grant")));
}

void TestTokenResponse::rejectsMalformedJson()
{
    QString error;
    const auto response = TokenResponse::fromJson("not json", &error);
    QVERIFY(!response.has_value());
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TestTokenResponse)
#include "test_tokenresponse.moc"
