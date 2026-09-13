#ifndef CREDENTIALSPROVIDER_H
#define CREDENTIALSPROVIDER_H

#include "keychainbackend.h"

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

    // `keychain`, when non-null, is used instead of a freshly constructed
    // real one — exists solely so unit tests can substitute a fake that
    // never touches the real OS credential store. Production code always
    // passes nullptr (AuthManager instead passes through its own, shared
    // with every CredentialsProvider it creates).
    explicit CredentialsProvider(QObject *parent = nullptr, KeychainBackend *keychain = nullptr);

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

    RealKeychainBackend m_realKeychainBackend; // used unless a test injects its own; declared before m_keychain
    KeychainBackend *m_keychain;
};

#endif // CREDENTIALSPROVIDER_H
