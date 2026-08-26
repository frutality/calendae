#ifndef MONTHEVENTITEM_H
#define MONTHEVENTITEM_H

#include <QColor>
#include <QDateTime>
#include <QString>

// A single event as prepared for display in one day cell. Deliberately
// decoupled from Event/Calendar: title already has the "(No title)"
// fallback applied, color already resolved from the owning calendar,
// timeLabel is pre-formatted for the current locale (empty for all-day).
struct MonthDayEventItem
{
    QString eventId;
    QString calendarId;
    QString title;
    QColor color;
    bool allDay = false;
    QString timeLabel; // e.g. "9:00 AM"; empty when allDay
    QDateTime startInstant; // invalid for all-day; used only for cross-calendar sort order
};

#endif // MONTHEVENTITEM_H
