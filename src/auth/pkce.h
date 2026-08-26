#ifndef PKCE_H
#define PKCE_H

#include <QByteArray>

namespace Pkce {

struct Pair
{
    QByteArray verifier;
    QByteArray challenge;
};

// Generates an RFC 7636 code_verifier/code_challenge pair (S256 method).
Pair generate();

// Cryptographically random, URL-safe (base64url, no padding) string built
// from numBytes of randomness. Also used for the OAuth "state" parameter.
QByteArray randomUrlSafeString(int numBytes);

} // namespace Pkce

#endif // PKCE_H
