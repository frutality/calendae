#ifndef FAKEKEYCHAINBACKEND_H
#define FAKEKEYCHAINBACKEND_H

#include "auth/keychainbackend.h"

#include <QHash>
#include <QString>
#include <QTimer>

// Stands in for the real OS keychain: every read/write/remove is answered
// from an in-memory map instead of the real Secret Service/KWallet/D-Bus —
// no real credential store is ever touched. Completes asynchronously (next
// event-loop turn, via QTimer::singleShot(0, ...)) so it behaves like the
// real, inherently-async QtKeychain jobs it replaces.
//
// Pre-seed `entries` directly for a "already has a stored X" scenario;
// inspect it afterward to assert what got written/removed.
class FakeKeychainBackend : public KeychainBackend
{
public:
    QHash<QString, QString> entries;

    // Returned for a read of a key not present in `entries`. Set this to a
    // different QKeychain::Error to simulate a backend failure (Retriable/
    // Fatal — see AuthManager::classifyKeychainReadError) instead of a
    // plain "not found".
    QKeychain::Error errorToReturnWhenMissing = QKeychain::EntryNotFound;

    void read(const QString &key, QObject *,
              const std::function<void(QKeychain::Error, const QString &)> &callback) override
    {
        const auto it = entries.constFind(key);
        if (it == entries.constEnd()) {
            const QKeychain::Error error = errorToReturnWhenMissing;
            QTimer::singleShot(0, [callback, error] { callback(error, QString()); });
            return;
        }
        const QString textData = it.value();
        QTimer::singleShot(0, [callback, textData] { callback(QKeychain::NoError, textData); });
    }

    void write(const QString &key, const QString &textData, QObject *,
               const std::function<void(QKeychain::Error)> &callback) override
    {
        entries[key] = textData;
        QTimer::singleShot(0, [callback] { callback(QKeychain::NoError); });
    }

    void remove(const QString &key, QObject *, const std::function<void(QKeychain::Error)> &callback) override
    {
        entries.remove(key);
        QTimer::singleShot(0, [callback] { callback(QKeychain::NoError); });
    }
};

#endif // FAKEKEYCHAINBACKEND_H
