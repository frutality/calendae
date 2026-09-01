#include "reminderschedule.h"

#include <QDate>
#include <QTime>

namespace {
const QLatin1String kPopupMethod("popup");
}

QString ReminderSchedule::keyFor(const QString &calendarId, const QString &eventId, int minutesBefore,
                                 const QDateTime &fireAt)
{
    return calendarId + QLatin1Char('\x1f') + eventId + QLatin1Char('\x1f') + QString::number(minutesBefore)
        + QLatin1Char('\x1f') + QString::number(fireAt.toMSecsSinceEpoch());
}

QList<PlannedReminder> ReminderSchedule::plan(const QList<Event> &events, const QDateTime &now, const QDateTime &windowEnd)
{
    QList<PlannedReminder> planned;

    for (const Event &event : events) {
        // All-day events have no clock time; Google measures their reminder
        // lead time from the start of the day. Local midnight is the right
        // anchor for a desktop client showing the user's local calendar.
        const QDateTime startLocal = event.allDay
            ? QDateTime(event.startDate, QTime(0, 0))
            : event.startDateTime.toLocalTime();
        if (!startLocal.isValid())
            continue;

        for (const EventReminder &reminder : event.reminderOverrides) {
            if (reminder.method != kPopupMethod)
                continue;

            const QDateTime fireAt = startLocal.addSecs(-qint64(reminder.minutes) * 60);
            if (fireAt <= now || fireAt > windowEnd)
                continue;

            PlannedReminder entry;
            entry.fireAt = fireAt;
            entry.reminder.calendarId = event.calendarId;
            entry.reminder.eventId = event.id;
            entry.reminder.title = event.summary;
            entry.reminder.startLocal = startLocal;
            entry.reminder.allDay = event.allDay;
            entry.reminder.minutesBefore = reminder.minutes;
            planned.append(entry);
        }
    }

    return planned;
}
