#ifndef CALENDAR_H
#define CALENDAR_H

#include <QColor>
#include <QList>
#include <QString>
#include <optional>

// A single entry from the user's Google CalendarList (not the raw Calendar
// resource — CalendarList is what represents "calendars this user has
// added", which is what the sidebar shows).
struct Calendar
{
    QString id; // calendarList entry id, e.g. an email address
    QString summary; // display name
    QColor color; // from backgroundColor; falls back to Qt::gray if missing/invalid
    bool selected = false; // "enabled for display"; Google omits this field when false
    QString accessRole; // "owner"/"writer"/"writerWithoutPrivateAccess"/"reader"/"freeBusyReader"; empty if absent
    bool primary = false; // Google omits this field when false

    // Parses a calendarList.list JSON response body (only the first page —
    // GoogleCalendarApi::fetchCalendarList does not follow nextPageToken).
    // Returns std::nullopt on malformed JSON, with errorOut filled in.
    static std::optional<QList<Calendar>> listFromJson(const QByteArray &json, QString *errorOut = nullptr);
};

#endif // CALENDAR_H
