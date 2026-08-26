#ifndef CREDENTIALSPROVIDER_H
#define CREDENTIALSPROVIDER_H

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

struct OAuthClientCredentials
{
    QString clientId;
    QString clientSecret;

    bool isValid() const { return !clientId.isEmpty() && !clientSecret.isEmpty(); }
};

// Resolves the Google OAuth "Desktop app" Client ID/Secret without ever
// hardcoding them in shipped source or config. Resolution order:
//   1. A config file at QStandardPaths::AppConfigLocation()/oauth_client.json
//   2. QtKeychain, if previously entered via the first-run dialog
//   3. (only if allowInteractiveFallback) a dialog prompting the user to
//      paste their own credentials, saved to QtKeychain afterwards
class CredentialsProvider : public QObject
{
    Q_OBJECT
public:
    static const QString keychainService;
    static const QString keychainKey;

    explicit CredentialsProvider(QObject *parent = nullptr);

    // Emits resolved() or failed() asynchronously (even when the config file
    // is used, to keep the calling contract uniform).
    void resolve(bool allowInteractiveFallback, QWidget *dialogParent = nullptr);

signals:
    void resolved(const OAuthClientCredentials &credentials);
    void failed(const QString &reason);

private:
    void tryConfigFile();
    void tryKeychain(bool allowInteractiveFallback, QWidget *dialogParent);
    void showDialogAndSave(QWidget *dialogParent);
};

#endif // CREDENTIALSPROVIDER_H
