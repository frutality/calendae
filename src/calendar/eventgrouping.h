#ifndef EVENTGROUPING_H
#define EVENTGROUPING_H

#include "calendar.h"
#include "event.h"
#include "montheventitem.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>

// Turns a flat list of Event (as returned by one events.list request) into
// display-ready MonthDayEventItem lists bucketed by every date each event
// covers. Shared by MonthEventsController (42-day month grid) and
// TimeGridEventsController (week/day ranges) — the grouping itself has no
// grid-shape dependency.
namespace EventGrouping {

QHash<QDate, QList<MonthDayEventItem>> groupByDate(const QList<Event> &events, const QHash<QString, Calendar> &calendarsById);

} // namespace EventGrouping

#endif // EVENTGROUPING_H
