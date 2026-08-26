#ifndef EVENTTIMERANGE_H
#define EVENTTIMERANGE_H

#include <QDate>
#include <QList>
#include <QString>

// Pure date-range math for building events.list's timeMin/timeMax bounds.
namespace EventTimeRange {

struct Range
{
    QString timeMin;
    QString timeMax;
};

// Turns an already-sorted-ascending, non-empty list of dates (e.g. a month
// grid's 42 visible dates) into the RFC3339 [timeMin, timeMax) bounds
// events.list expects: local midnight of the first date, up to (but
// excluding) local midnight of the day after the last date. Always
// produces UTC ("Z"-suffixed) strings — QDateTime's ISODate formatting
// only appends an offset for UTC/OffsetFromUTC specs, not for a bare
// Qt::LocalTime-speced QDateTime.
Range forDates(const QList<QDate> &dates);

} // namespace EventTimeRange

#endif // EVENTTIMERANGE_H
