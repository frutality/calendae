#include "pkce.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace Pkce {

QByteArray randomUrlSafeString(int numBytes)
{
    QByteArray bytes;
    bytes.reserve(numBytes);
    for (int i = 0; i < numBytes; ++i)
        bytes.append(static_cast<char>(QRandomGenerator::system()->bounded(256)));

    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

Pair generate()
{
    Pair pair;
    pair.verifier = randomUrlSafeString(64);
    pair.challenge = QCryptographicHash::hash(pair.verifier, QCryptographicHash::Sha256)
                          .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return pair;
}

} // namespace Pkce
