#ifndef OAUTHCREDENTIALSDIALOG_H
#define OAUTHCREDENTIALSDIALOG_H

#include <QDialog>

namespace Ui {
class OAuthCredentialsDialog;
}

class OAuthCredentialsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OAuthCredentialsDialog(QWidget *parent = nullptr);
    ~OAuthCredentialsDialog() override;

    QString clientId() const;
    QString clientSecret() const;

private slots:
    void updateOkEnabled();

private:
    Ui::OAuthCredentialsDialog *ui;
};

#endif // OAUTHCREDENTIALSDIALOG_H
