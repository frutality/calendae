#include "tokenresponse.h"

#include <QJsonDocument>
#include <QJsonObject>

std::optional<TokenResponse> TokenResponse::fromJson(const QByteArray &json, QString *errorOut)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Malformed token response: %1").arg(parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject obj = doc.object();

    if (obj.contains(QStringLiteral("error"))) {
        if (errorOut) {
            const QString error = obj.value(QStringLiteral("error")).toString();
            const QString description = obj.value(QStringLiteral("error_description")).toString();
            *errorOut = description.isEmpty() ? error : QStringLiteral("%1: %2").arg(error, description);
        }
        return std::nullopt;
    }

    if (!obj.contains(QStringLiteral("access_token")) || !obj.contains(QStringLiteral("expires_in"))) {
        if (errorOut)
            *errorOut = QStringLiteral("Token response is missing required fields");
        return std::nullopt;
    }

    TokenResponse response;
    response.accessToken = obj.value(QStringLiteral("access_token")).toString();
    response.refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
    response.scope = obj.value(QStringLiteral("scope")).toString();

    const qint64 expiresInSeconds = static_cast<qint64>(obj.value(QStringLiteral("expires_in")).toDouble());
    response.expiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(expiresInSeconds);

    return response;
}
