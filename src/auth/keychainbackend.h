#ifndef KEYCHAINBACKEND_H
#define KEYCHAINBACKEND_H

#include <keychain.h>

#include <QObject>
#include <QString>
#include <functional>

// Abstracts the three keychain operations AuthManager/CredentialsProvider
// need, so unit tests can substitute a fake that never touches the real OS
// credential store. This matters more than it sounds: on a machine with a
// real Secret Service/KWallet backend reachable, QtKeychain's
// setInsecureFallback(true) is NOT consulted for an ordinary found/not-found/
// write/delete — those go straight to the real backend under the real
// service name ("calendae", the same one the installed app uses). Testing
// against the real makeKeychainJob() path once actually deleted a
// developer's real stored Google session — see keychainjob.h's callers.
//
// `parent`, when non-null, is used both as the underlying QKeychain job's
// QObject parent and as the callback's connection context: passing the
// object whose lifetime the callback's captures depend on (typically the
// caller itself) makes the callback safely never fire once that object is
// destroyed — the same guarantee a direct signal/slot connection gives.
// Passing nullptr means "let this fire-and-forget, independent of whoever
// called it" — only safe when the callback captures nothing that might
// outlive the call (see CredentialsProvider::showDialogAndSave()).
class KeychainBackend
{
public:
    virtual ~KeychainBackend() = default;

    virtual void read(const QString &key, QObject *parent,
                       const std::function<void(QKeychain::Error, const QString &textData)> &callback) = 0;
    virtual void write(const QString &key, const QString &textData, QObject *parent,
                        const std::function<void(QKeychain::Error)> &callback) = 0;
    virtual void remove(const QString &key, QObject *parent,
                         const std::function<void(QKeychain::Error)> &callback) = 0;
};

// Production implementation: every call goes to the real OS credential store
// via QtKeychain (through keychainjob.h's makeKeychainJob(), so the storage
// policy — Secret Service/KWallet, insecure fallback — is unchanged from
// before this abstraction existed). Stateless: it doesn't own or track the
// jobs it creates, so its own lifetime has no bearing on whether an
// in-flight call is safe to complete.
class RealKeychainBackend : public KeychainBackend
{
public:
    void read(const QString &key, QObject *parent,
               const std::function<void(QKeychain::Error, const QString &textData)> &callback) override;
    void write(const QString &key, const QString &textData, QObject *parent,
                const std::function<void(QKeychain::Error)> &callback) override;
    void remove(const QString &key, QObject *parent,
                 const std::function<void(QKeychain::Error)> &callback) override;
};

#endif // KEYCHAINBACKEND_H
