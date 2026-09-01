#include "event.h"

#include <QCborArray>
#include <QCborValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTime>

namespace {
Q_LOGGING_CATEGORY(lcEvent, "tgc.event")

// ISO-8601 with the offset preserved (Qt::ISODate keeps the trailing
// "Z"/"+hh:mm"), matching what QDateTime::fromString(..., Qt::ISODate)
// round-trips. An invalid QDate/QDateTime serializes to an empty string.
QString dateToString(const QDate &date)
{
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}
QString dateTimeToString(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toString(Qt::ISODate) : QString();
}
} // namespace

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

        if (obj.contains(QStringLiteral("reminders"))) {
            const QJsonObject reminders = obj.value(QStringLiteral("reminders")).toObject();
            event.remindersUseDefault = reminders.value(QStringLiteral("useDefault")).toBool(true);
            const QJsonArray overrides = reminders.value(QStringLiteral("overrides")).toArray();
            for (const QJsonValue &overrideValue : overrides) {
                const QJsonObject overrideObj = overrideValue.toObject();
                EventReminder reminder;
                reminder.method = overrideObj.value(QStringLiteral("method")).toString();
                reminder.minutes = overrideObj.value(QStringLiteral("minutes")).toInt();
                if (!reminder.method.isEmpty())
                    event.reminderOverrides.append(reminder);
            }
        }

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

QCborMap Event::toCbor() const
{
    QCborArray reminders;
    for (const EventReminder &reminder : reminderOverrides) {
        reminders.append(QCborMap{
            {QStringLiteral("method"), reminder.method},
            {QStringLiteral("minutes"), reminder.minutes},
        });
    }

    // startDate/endDate are stored even for timed events (where they're
    // derived from the local-time date of the start/end): keeping them lets
    // fromCbor stay a plain field copy instead of re-deriving them, so the
    // derivation rule lives in exactly one place (listFromJson).
    return QCborMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("calendarId"), calendarId},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("description"), description},
        {QStringLiteral("recurringEventId"), recurringEventId},
        {QStringLiteral("allDay"), allDay},
        {QStringLiteral("startDate"), dateToString(startDate)},
        {QStringLiteral("endDate"), dateToString(endDate)},
        {QStringLiteral("startDateTime"), dateTimeToString(startDateTime)},
        {QStringLiteral("endDateTime"), dateTimeToString(endDateTime)},
        {QStringLiteral("remindersUseDefault"), remindersUseDefault},
        {QStringLiteral("reminderOverrides"), reminders},
    };
}

std::optional<Event> Event::fromCbor(const QCborMap &map)
{
    Event event;
    event.id = map.value(QStringLiteral("id")).toString();
    if (event.id.isEmpty())
        return std::nullopt;

    event.calendarId = map.value(QStringLiteral("calendarId")).toString();
    event.summary = map.value(QStringLiteral("summary")).toString();
    event.description = map.value(QStringLiteral("description")).toString();
    event.recurringEventId = map.value(QStringLiteral("recurringEventId")).toString();
    event.allDay = map.value(QStringLiteral("allDay")).toBool();
    event.startDate = QDate::fromString(map.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
    event.endDate = QDate::fromString(map.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
    event.startDateTime = QDateTime::fromString(map.value(QStringLiteral("startDateTime")).toString(), Qt::ISODate);
    event.endDateTime = QDateTime::fromString(map.value(QStringLiteral("endDateTime")).toString(), Qt::ISODate);

    // Same validity gate as listFromJson: an all-day event needs its date
    // pair, a timed event its dateTime pair. Anything else is a corrupt
    // cache entry and is dropped.
    if (event.allDay) {
        if (!event.startDate.isValid() || !event.endDate.isValid())
            return std::nullopt;
    } else {
        if (!event.startDateTime.isValid() || !event.endDateTime.isValid())
            return std::nullopt;
    }

    event.remindersUseDefault = map.value(QStringLiteral("remindersUseDefault")).toBool(true);
    const QCborArray reminders = map.value(QStringLiteral("reminderOverrides")).toArray();
    for (const QCborValue &value : reminders) {
        const QCborMap reminderMap = value.toMap();
        EventReminder reminder;
        reminder.method = reminderMap.value(QStringLiteral("method")).toString();
        reminder.minutes = reminderMap.value(QStringLiteral("minutes")).toInteger();
        if (!reminder.method.isEmpty())
            event.reminderOverrides.append(reminder);
    }

    return event;
}

QDate Event::lastInclusiveLocalDate() const
{
    QDate last;
    if (allDay) {
        last = endDate.addDays(-1);
    } else {
        const QDateTime localEnd = endDateTime.toLocalTime();
        last = localEnd.date();
        // Ending at exactly 00:00 means the event closes at the very start
        // of that day and occupies none of it, so the last day it actually
        // covers is the one before.
        if (localEnd.time() == QTime(0, 0) && last > startDate)
            last = last.addDays(-1);
    }
    return last < startDate ? startDate : last;
}
