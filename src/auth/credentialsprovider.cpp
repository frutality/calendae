#include "credentialsprovider.h"
#include "oauthcredentialsdialog.h"

#include <keychain.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

const QString CredentialsProvider::keychainService = QStringLiteral("calendae");
const QString CredentialsProvider::keychainKey = QStringLiteral("oauth_client_credentials");

CredentialsProvider::CredentialsProvider(QObject *parent, KeychainBackend *keychain)
    : QObject(parent)
    , m_keychain(keychain ? keychain : &m_realKeychainBackend)
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
    m_keychain->read(keychainKey, this, [this, allowInteractiveFallback, dialogParent](QKeychain::Error error, const QString &textData) {
        if (error == QKeychain::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(textData.toUtf8());
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

        // parent=nullptr: the caller deletes this CredentialsProvider as
        // soon as resolved() is emitted below, which would abort this async
        // write mid-flight if it were tied to `this`'s lifetime. The
        // underlying job self-deletes (autoDelete()) once finished() fires,
        // so this is intentional fire-and-forget, not a leak.
        m_keychain->write(keychainKey, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)),
                          nullptr, [](QKeychain::Error) {});

        emit resolved(creds);
    });
    dialog->open();
}
