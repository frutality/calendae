#include "monthgrid.h"

QList<QDate> MonthGrid::datesForGrid(const QDate &anyDateInMonth, Qt::DayOfWeek firstDayOfWeek)
{
    const QDate firstOfMonth(anyDateInMonth.year(), anyDateInMonth.month(), 1);
    const int firstOfMonthDow = firstOfMonth.dayOfWeek(); // 1=Monday..7=Sunday
    const int leadingDays = (firstOfMonthDow - static_cast<int>(firstDayOfWeek) + 7) % 7;
    const QDate gridStart = firstOfMonth.addDays(-leadingDays);

    QList<QDate> dates;
    dates.reserve(42);
    for (int i = 0; i < 42; ++i)
        dates.append(gridStart.addDays(i));
    return dates;
}
