#ifndef REMINDERSCHEDULE_H
#define REMINDERSCHEDULE_H

#include "event.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

// Everything the desktop notification needs to render itself, decoupled
// from Event/Calendar (title carries the raw summary; the presenter applies
// the "(No title)" fallback).
struct DueReminder
{
    QString calendarId;
    QString eventId;
    QString title; // raw event.summary, MAY be empty
    QDateTime startLocal; // event start, local time
    bool allDay = false;
    int minutesBefore = 0; // the override's lead time, for the notification body
};

// One armed reminder: fire at fireAt (local time), then deliver reminder.
struct PlannedReminder
{
    QDateTime fireAt;
    DueReminder reminder;
};

namespace ReminderSchedule {

// Expands every event's popup reminder overrides into concrete fire times.
// Skips: non-"popup" methods; fire times at or before now (a reminder
// missed while the app was closed is dropped, never replayed); fire times
// after windowEnd (a later scheduling pass, once its window has advanced,
// will pick them up). All-day events anchor to local midnight of their
// startDate. Input order is preserved.
QList<PlannedReminder> plan(const QList<Event> &events, const QDateTime &now, const QDateTime &windowEnd);

// Stable per-occurrence identity
// ("<calendarId>\x1f<eventId>\x1f<minutes>\x1f<fireAt ms>"), used to
// remember which reminders already fired this session. fireAt is part of
// the key on purpose: if an event is rescheduled its reminder fires at a
// new time and must not be suppressed by the earlier occurrence having
// fired.
QString keyFor(const QString &calendarId, const QString &eventId, int minutesBefore, const QDateTime &fireAt);

} // namespace ReminderSchedule

Q_DECLARE_METATYPE(DueReminder)

#endif // REMINDERSCHEDULE_H
