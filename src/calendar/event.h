#ifndef EVENT_H
#define EVENT_H

#include <QCborMap>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

// One entry of an event's reminders.overrides array. method is kept raw
// ("popup", "email", "sms", …) — only "popup" drives this client's desktop
// notifications, but non-popup entries are preserved verbatim so editing an
// event doesn't silently drop reminders this app can't display.
struct EventReminder
{
    QString method;
    int minutes = 0; // lead time before the event start
};

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

    // reminders.useDefault: true (or the field is absent) means the event
    // inherits its calendar's default reminders — which this client
    // deliberately does NOT act on. Only reminderOverrides with
    // method == "popup" raise a desktop notification.
    bool remindersUseDefault = true;
    QList<EventReminder> reminderOverrides; // reminders.overrides, verbatim

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

    // Round-trips an already-parsed Event through CBOR for the on-disk event
    // cache (EventCacheStore). Deliberately separate from listFromJson: that
    // one parses Google's wire format, this one persists our own struct, so
    // the two can evolve independently. fromCbor applies the same validity
    // gate as listFromJson — a missing id, or neither a valid date pair nor
    // a valid dateTime pair, yields std::nullopt so a corrupt entry is
    // skipped rather than resurrected half-populated.
    QCborMap toCbor() const;
    static std::optional<Event> fromCbor(const QCborMap &map);
};

#endif // EVENT_H
