#include "oauthcredentialsdialog.h"
#include "ui_oauthcredentialsdialog.h"

#include <QPushButton>

OAuthCredentialsDialog::OAuthCredentialsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OAuthCredentialsDialog)
{
    ui->setupUi(this);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(ui->clientIdEdit, &QLineEdit::textChanged, this, &OAuthCredentialsDialog::updateOkEnabled);
    connect(ui->clientSecretEdit, &QLineEdit::textChanged, this, &OAuthCredentialsDialog::updateOkEnabled);
}

OAuthCredentialsDialog::~OAuthCredentialsDialog()
{
    delete ui;
}

QString OAuthCredentialsDialog::clientId() const
{
    return ui->clientIdEdit->text().trimmed();
}

QString OAuthCredentialsDialog::clientSecret() const
{
    return ui->clientSecretEdit->text().trimmed();
}

void OAuthCredentialsDialog::updateOkEnabled()
{
    const bool valid = !ui->clientIdEdit->text().trimmed().isEmpty()
        && !ui->clientSecretEdit->text().trimmed().isEmpty();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}
