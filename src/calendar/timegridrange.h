#ifndef TIMEGRIDRANGE_H
#define TIMEGRIDRANGE_H

#include <QDate>
#include <QList>
#include <Qt>

// Pure date math for the week/day time-grid views, no widget dependency.
namespace TimeGridRange {

// Snaps anyDateInWeek back to the first day of its week, per firstDayOfWeek.
QDate weekStart(const QDate &anyDateInWeek, Qt::DayOfWeek firstDayOfWeek);

// Returns dayCount consecutive dates starting at startDate (dayCount == 1
// for day view, 7 for week view).
QList<QDate> datesForRange(const QDate &startDate, int dayCount);

} // namespace TimeGridRange

#endif // TIMEGRIDRANGE_H
