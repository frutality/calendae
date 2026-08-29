#include "monthkeys.h"

QDate MonthKeys::normalize(const QDate &anyDateInMonth)
{
    if (!anyDateInMonth.isValid())
        return {};
    return QDate(anyDateInMonth.year(), anyDateInMonth.month(), 1);
}

QList<QDate> MonthKeys::forDateRange(const QDate &from, const QDate &to)
{
    if (!from.isValid() || !to.isValid() || to < from)
        return {};

    QList<QDate> months;
    for (QDate month = normalize(from); month <= to; month = month.addMonths(1))
        months.append(month);
    return months;
}
