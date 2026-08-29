#ifndef MONTHKEYS_H
#define MONTHKEYS_H

#include <QDate>
#include <QList>

// Pure date math: the set of calendar months a date range covers, each
// represented by its first-of-month QDate. Maps a view's visible date range
// (a week or a day) onto the month-granularity buckets MonthEventStore
// fetches and caches. No widget dependency.
namespace MonthKeys {

// First day of the month containing anyDateInMonth. Invalid in, invalid out.
QDate normalize(const QDate &anyDateInMonth);

// First-of-month for every calendar month touched by [from, to] inclusive,
// ascending. from/to need not be first-of-month. Returns an empty list if
// either date is invalid or to < from.
QList<QDate> forDateRange(const QDate &from, const QDate &to);

} // namespace MonthKeys

#endif // MONTHKEYS_H
