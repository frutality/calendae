#include "timegridrange.h"

QDate TimeGridRange::weekStart(const QDate &anyDateInWeek, Qt::DayOfWeek firstDayOfWeek)
{
    const int dow = anyDateInWeek.dayOfWeek(); // 1=Monday..7=Sunday
    const int leadingDays = (dow - static_cast<int>(firstDayOfWeek) + 7) % 7;
    return anyDateInWeek.addDays(-leadingDays);
}

QList<QDate> TimeGridRange::datesForRange(const QDate &startDate, int dayCount)
{
    QList<QDate> dates;
    dates.reserve(dayCount);
    for (int i = 0; i < dayCount; ++i)
        dates.append(startDate.addDays(i));
    return dates;
}
