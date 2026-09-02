#include "credentialsprovider.h"
#include "keychainjob.h"
#include "oauthcredentialsdialog.h"

#include <keychain.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

const QString CredentialsProvider::keychainService = QStringLiteral("calendae");
const QString CredentialsProvider::keychainKey = QStringLiteral("oauth_client_credentials");

CredentialsProvider::CredentialsProvider(QObject *parent)
    : QObject(parent)
{
}

void CredentialsProvider::resolve(bool allowInteractiveFallback, QWidget *dialogParent)
{
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/oauth_client.json");

    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject obj = doc.object();
        OAuthClientCredentials creds;
        creds.clientId = obj.value(QStringLiteral("client_id")).toString();
        creds.clientSecret = obj.value(QStringLiteral("client_secret")).toString();
        if (creds.isValid()) {
            QTimer::singleShot(0, this, [this, creds] { emit resolved(creds); });
            return;
        }
    }

    tryKeychain(allowInteractiveFallback, dialogParent);
}

void CredentialsProvider::tryKeychain(bool allowInteractiveFallback, QWidget *dialogParent)
{
    auto *job = makeKeychainJob<QKeychain::ReadPasswordJob>(keychainKey, this);
    connect(job, &QKeychain::Job::finished, this, [this, allowInteractiveFallback, dialogParent](QKeychain::Job *job) {
        auto *readJob = qobject_cast<QKeychain::ReadPasswordJob *>(job);
        if (readJob->error() == QKeychain::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(readJob->textData().toUtf8());
            const QJsonObject obj = doc.object();
            OAuthClientCredentials creds;
            creds.clientId = obj.value(QStringLiteral("client_id")).toString();
            creds.clientSecret = obj.value(QStringLiteral("client_secret")).toString();
            if (creds.isValid()) {
                emit resolved(creds);
                return;
            }
        }

        if (allowInteractiveFallback) {
            showDialogAndSave(dialogParent);
        } else {
            emit failed(tr("No Google OAuth client credentials are configured."));
        }
    });
    job->start();
}

void CredentialsProvider::showDialogAndSave(QWidget *dialogParent)
{
    auto *dialog = new OAuthCredentialsDialog(dialogParent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        if (result != QDialog::Accepted) {
            emit failed(tr("Sign-in requires Google OAuth client credentials."));
            return;
        }

        OAuthClientCredentials creds;
        creds.clientId = dialog->clientId();
        creds.clientSecret = dialog->clientSecret();

        const QJsonObject obj{
            {QStringLiteral("client_id"), creds.clientId},
            {QStringLiteral("client_secret"), creds.clientSecret},
        };

        // Not parented to `this`: the caller deletes this CredentialsProvider
        // as soon as resolved() is emitted below, which would abort this
        // async write mid-flight if it were a child of `this`. QtKeychain
        // jobs self-delete (autoDelete()) once finished() fires, so a null
        // parent here is intentional, not a leak.
        auto *writeJob = makeKeychainJob<QKeychain::WritePasswordJob>(keychainKey);
        writeJob->setTextData(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        writeJob->start();

        emit resolved(creds);
    });
    dialog->open();
}
