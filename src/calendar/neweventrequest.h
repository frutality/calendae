#ifndef NEWEVENTREQUEST_H
#define NEWEVENTREQUEST_H

#include "event.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

// Caller-supplied fields for GoogleCalendarApi::createEvent. Only the
// allDay-relevant fields are read/serialized — same allDay-gated
// QDate-pair-vs-QDateTime-pair convention as Event (event.h). Single-day
// only for M5 (Google's Event data listing has one "date" field); start/end
// are kept independent so multi-day creation later is additive, not a
// restructure.
struct NewEventRequest
{
    // How the dialog's single "Remind me" control maps onto the request body:
    // - Unchanged: omit "reminders" entirely. On create Google applies the
    //   calendar default; on PATCH the event's existing reminders are left
    //   untouched.
    // - Off: send {useDefault:false, overrides:<preservedReminderOverrides>}
    //   — clears any popup reminder while keeping email/sms ones.
    // - Popup: send {useDefault:false, overrides:<preservedReminderOverrides>
    //   + one {method:"popup", minutes:popupReminderMinutes}}.
    enum class ReminderMode { Unchanged, Off, Popup };

    QString calendarId;
    QString summary;
    QString description; // may be empty; omitted from the request body when empty
    bool allDay = false;
    QDate startDate; // used when allDay
    QDate endDateExclusive; // used when allDay (Google semantics, see Event::endDate)
    QDateTime startDateTime; // used when !allDay; must carry a QTimeZone::LocalTime spec
    QDateTime endDateTime; // used when !allDay; must carry a QTimeZone::LocalTime spec

    ReminderMode reminderMode = ReminderMode::Unchanged;
    int popupReminderMinutes = 0; // used when reminderMode == Popup
    QList<EventReminder> preservedReminderOverrides; // non-popup overrides carried through on Off/Popup
};

#endif // NEWEVENTREQUEST_H
