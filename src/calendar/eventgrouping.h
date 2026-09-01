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

// True if the event (calendarId, eventId) appears in groupedByCalendar on
// at least one of dates. The grid views call this after a background data
// refresh to decide whether to keep the current click-selection: an event
// a refresh moved off the rendered range — or removed outright — must not
// keep a selection the user can no longer see, otherwise Delete-Selected
// would act on an off-screen event.
bool containsEventOnAnyDate(const QHash<QString, QHash<QDate, QList<MonthDayEventItem>>> &groupedByCalendar,
                            const QString &calendarId,
                            const QString &eventId,
                            const QList<QDate> &dates);

} // namespace EventGrouping

#endif // EVENTGROUPING_H
