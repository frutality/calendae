#include "keychainjob.h"

#include <QSettings>
#include <QStringList>

bool keychainSecretStoredInsecurely()
{
    // QtKeychain's insecure fallback (PlainTextStore) writes to
    // QSettings(service); on Unix every secret becomes a "<key>/data" entry
    // (alongside "<key>/type"). If any such entry exists, at least one write
    // landed there because no OS keyring answered.
    QSettings settings(CredentialsProvider::keychainService);
    const QStringList groups = settings.childGroups();
    for (const QString &group : groups) {
        if (settings.contains(group + QStringLiteral("/data")))
            return true;
    }
    return false;
}
