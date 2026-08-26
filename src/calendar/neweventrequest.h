#ifndef NEWEVENTREQUEST_H
#define NEWEVENTREQUEST_H

#include <QDate>
#include <QDateTime>
#include <QString>

// Caller-supplied fields for GoogleCalendarApi::createEvent. Only the
// allDay-relevant fields are read/serialized — same allDay-gated
// QDate-pair-vs-QDateTime-pair convention as Event (event.h). Single-day
// only for M5 (Google's Event data listing has one "date" field); start/end
// are kept independent so multi-day creation later is additive, not a
// restructure.
struct NewEventRequest
{
    QString calendarId;
    QString summary;
    QString description; // may be empty; omitted from the request body when empty
    bool allDay = false;
    QDate startDate; // used when allDay
    QDate endDateExclusive; // used when allDay (Google semantics, see Event::endDate)
    QDateTime startDateTime; // used when !allDay; must carry a QTimeZone::LocalTime spec
    QDateTime endDateTime; // used when !allDay; must carry a QTimeZone::LocalTime spec
};

#endif // NEWEVENTREQUEST_H
