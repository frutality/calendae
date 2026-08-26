#ifndef MONTHGRID_H
#define MONTHGRID_H

#include <QDate>
#include <QList>

// Pure date math for a month-view grid, no widget dependency.
namespace MonthGrid {

// Returns the 42 consecutive dates (6 weeks, row-major, 7 per row) that
// fill the grid for the month containing anyDateInMonth, always starting
// on firstDayOfWeek and always exactly 42 dates regardless of how many
// leading/trailing adjacent-month days that requires.
QList<QDate> datesForGrid(const QDate &anyDateInMonth, Qt::DayOfWeek firstDayOfWeek);

} // namespace MonthGrid

#endif // MONTHGRID_H
