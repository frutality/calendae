#include "eventtimerange.h"

#include <QDateTime>
#include <QTime>
#include <QTimeZone>

EventTimeRange::Range EventTimeRange::forDates(const QList<QDate> &dates)
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    const QDateTime start(dates.first(), QTime(0, 0), localTimeZone);
    const QDateTime endExclusive(dates.last().addDays(1), QTime(0, 0), localTimeZone);
    return Range{start.toUTC().toString(Qt::ISODate), endExclusive.toUTC().toString(Qt::ISODate)};
}
