#include "event.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcEvent, "tgc.event")
}

std::optional<QList<Event>> Event::listFromJson(const QByteArray &json,
                                                 const QString &calendarId,
                                                 QString *nextPageTokenOut,
                                                 QString *errorOut)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Malformed events response: %1").arg(parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("items")) || !root.value(QStringLiteral("items")).isArray()) {
        if (errorOut)
            *errorOut = QStringLiteral("events response is missing an \"items\" array");
        return std::nullopt;
    }

    QList<Event> events;
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            qCWarning(lcEvent) << "Skipping event with missing id";
            continue;
        }

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();

        Event event;
        event.id = id;
        event.calendarId = calendarId;
        event.summary = obj.value(QStringLiteral("summary")).toString();
        event.description = obj.value(QStringLiteral("description")).toString();
        event.recurringEventId = obj.value(QStringLiteral("recurringEventId")).toString();

        if (startObj.contains(QStringLiteral("date")) && endObj.contains(QStringLiteral("date"))) {
            const QDate startDate = QDate::fromString(startObj.value(QStringLiteral("date")).toString(), Qt::ISODate);
            const QDate endDate = QDate::fromString(endObj.value(QStringLiteral("date")).toString(), Qt::ISODate);
            if (!startDate.isValid() || !endDate.isValid()) {
                qCWarning(lcEvent) << "Skipping all-day event with invalid date" << id;
                continue;
            }
            event.allDay = true;
            event.startDate = startDate;
            event.endDate = endDate;
        } else if (startObj.contains(QStringLiteral("dateTime")) && endObj.contains(QStringLiteral("dateTime"))) {
            const QDateTime startDateTime = QDateTime::fromString(startObj.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
            const QDateTime endDateTime = QDateTime::fromString(endObj.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
            if (!startDateTime.isValid() || !endDateTime.isValid()) {
                qCWarning(lcEvent) << "Skipping timed event with invalid dateTime" << id;
                continue;
            }
            event.allDay = false;
            event.startDateTime = startDateTime;
            event.endDateTime = endDateTime;
            event.startDate = startDateTime.toLocalTime().date();
            event.endDate = endDateTime.toLocalTime().date();
        } else {
            qCWarning(lcEvent) << "Skipping event with neither date nor dateTime" << id;
            continue;
        }

        events.append(event);
    }

    if (nextPageTokenOut)
        *nextPageTokenOut = root.value(QStringLiteral("nextPageToken")).toString();

    return events;
}
