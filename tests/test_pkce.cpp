#include "auth/pkce.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTest>

class TestPkce : public QObject
{
    Q_OBJECT
private slots:
    void verifierIsUrlSafe();
    void challengeMatchesSha256OfVerifier();
    void randomStringsAreUnique();
};

void TestPkce::verifierIsUrlSafe()
{
    const Pkce::Pair pair = Pkce::generate();
    static const QRegularExpression urlSafe(QStringLiteral("^[A-Za-z0-9_-]+$"));
    QVERIFY(urlSafe.match(QString::fromUtf8(pair.verifier)).hasMatch());
    // RFC 7636 requires a verifier of 43-128 characters.
    QVERIFY(pair.verifier.size() >= 43);
    QVERIFY(pair.verifier.size() <= 128);
}

void TestPkce::challengeMatchesSha256OfVerifier()
{
    const Pkce::Pair pair = Pkce::generate();
    const QByteArray expected = QCryptographicHash::hash(pair.verifier, QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QCOMPARE(pair.challenge, expected);
}

void TestPkce::randomStringsAreUnique()
{
    const QByteArray a = Pkce::randomUrlSafeString(16);
    const QByteArray b = Pkce::randomUrlSafeString(16);
    QVERIFY(a != b);
}

QTEST_APPLESS_MAIN(TestPkce)
#include "test_pkce.moc"
