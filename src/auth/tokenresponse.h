#ifndef TOKENRESPONSE_H
#define TOKENRESPONSE_H

#include <QDateTime>
#include <QString>
#include <optional>

struct TokenResponse
{
    QString accessToken;
    QString refreshToken; // empty if not present in the response
    QDateTime expiresAtUtc;
    QString scope;

    // Parses a Google OAuth token-endpoint JSON response. On failure (a
    // Google {"error": "...", "error_description": "..."} payload, or a
    // malformed/unparsable body), returns std::nullopt and, if errorOut is
    // non-null, fills it with a human-readable reason.
    static std::optional<TokenResponse> fromJson(const QByteArray &json, QString *errorOut = nullptr);
};

#endif // TOKENRESPONSE_H
