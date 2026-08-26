#include "calendar.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcCalendar, "tgc.calendar")
}

std::optional<QList<Calendar>> Calendar::listFromJson(const QByteArray &json, QString *errorOut)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Malformed calendarList response: %1").arg(parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("items")) || !root.value(QStringLiteral("items")).isArray()) {
        if (errorOut)
            *errorOut = QStringLiteral("calendarList response is missing an \"items\" array");
        return std::nullopt;
    }

    QList<Calendar> calendars;
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        const QString summary = obj.value(QStringLiteral("summary")).toString();
        if (id.isEmpty() || summary.isEmpty()) {
            qCWarning(lcCalendar) << "Skipping calendarList entry with missing id/summary";
            continue;
        }

        Calendar calendar;
        calendar.id = id;
        calendar.summary = summary;
        calendar.color = QColor(obj.value(QStringLiteral("backgroundColor")).toString());
        if (!calendar.color.isValid())
            calendar.color = QColor(Qt::gray);
        calendar.selected = obj.value(QStringLiteral("selected")).toBool(false);
        calendar.accessRole = obj.value(QStringLiteral("accessRole")).toString();
        calendar.primary = obj.value(QStringLiteral("primary")).toBool(false);
        calendars.append(calendar);
    }

    return calendars;
}
