#ifndef EVENT_H
#define EVENT_H

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

// One concrete occurrence of an event (recurring events are already
// expanded into individual instances by events.list's singleEvents=true,
// so every Event here is a one-off, never a recurring series).
struct Event
{
    QString id;
    QString calendarId; // supplied by the caller, not present per-item in the JSON
    QString summary; // raw Google value, MAY be empty — display fallback is applied elsewhere
    QString description; // raw Google value, MAY be empty
    QString recurringEventId; // empty unless this is an occurrence of a recurring series
    bool allDay = false;
    QDate startDate;
    QDate endDate; // exclusive, per Google Calendar semantics
    QDateTime startDateTime; // valid only if !allDay
    QDateTime endDateTime; // valid only if !allDay

    // Parses one page of an events.list JSON response body. calendarId is
    // stamped onto every parsed Event (it isn't part of the per-item JSON).
    // nextPageTokenOut, if non-null, is set to the page's nextPageToken
    // (empty string if absent) whenever parsing succeeds. Items missing an
    // id, or with neither a usable "date" nor "dateTime" start/end, are
    // skipped rather than failing the whole batch — a missing/empty
    // "summary" is NOT a skip reason (untitled events are legitimate).
    // Returns std::nullopt on malformed JSON, with errorOut filled in.
    static std::optional<QList<Event>> listFromJson(const QByteArray &json,
                                                      const QString &calendarId,
                                                      QString *nextPageTokenOut = nullptr,
                                                      QString *errorOut = nullptr);
};

#endif // EVENT_H
