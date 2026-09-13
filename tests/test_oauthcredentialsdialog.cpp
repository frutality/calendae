#include "auth/oauthcredentialsdialog.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>

namespace {
QPushButton *okButton(const OAuthCredentialsDialog &dialog)
{
    auto *box = dialog.findChild<QDialogButtonBox *>();
    return box->button(QDialogButtonBox::Ok);
}
} // namespace

class TestOAuthCredentialsDialog : public QObject
{
    Q_OBJECT
private slots:
    void okButtonDisabledUntilBothFieldsAreFilled();
    void okButtonDisabledAgainWhenAFieldIsCleared();
    void clientIdAndClientSecretAreTrimmed();
};

void TestOAuthCredentialsDialog::okButtonDisabledUntilBothFieldsAreFilled()
{
    OAuthCredentialsDialog dialog;
    QVERIFY(!okButton(dialog)->isEnabled());

    auto *clientIdEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clientIdEdit"));
    auto *clientSecretEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clientSecretEdit"));
    QVERIFY(clientIdEdit);
    QVERIFY(clientSecretEdit);

    clientIdEdit->setText(QStringLiteral("id-123"));
    QVERIFY(!okButton(dialog)->isEnabled()); // secret still empty

    clientSecretEdit->setText(QStringLiteral("secret-456"));
    QVERIFY(okButton(dialog)->isEnabled());
}

void TestOAuthCredentialsDialog::okButtonDisabledAgainWhenAFieldIsCleared()
{
    OAuthCredentialsDialog dialog;
    auto *clientIdEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clientIdEdit"));
    auto *clientSecretEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clientSecretEdit"));

    clientIdEdit->setText(QStringLiteral("id-123"));
    clientSecretEdit->setText(QStringLiteral("secret-456"));
    QVERIFY(okButton(dialog)->isEnabled());

    clientIdEdit->setText(QStringLiteral("   ")); // whitespace-only counts as empty
    QVERIFY(!okButton(dialog)->isEnabled());
}

void TestOAuthCredentialsDialog::clientIdAndClientSecretAreTrimmed()
{
    OAuthCredentialsDialog dialog;
    auto *clientIdEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clientIdEdit"));
    auto *clientSecretEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clientSecretEdit"));

    clientIdEdit->setText(QStringLiteral("  id-123  "));
    clientSecretEdit->setText(QStringLiteral("  secret-456  "));

    QCOMPARE(dialog.clientId(), QStringLiteral("id-123"));
    QCOMPARE(dialog.clientSecret(), QStringLiteral("secret-456"));
}

QTEST_MAIN(TestOAuthCredentialsDialog)
#include "test_oauthcredentialsdialog.moc"
