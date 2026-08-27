#include "eventgrouping.h"

#include <QLocale>
#include <QObject>

QHash<QDate, QList<MonthDayEventItem>> EventGrouping::groupByDate(const QList<Event> &events,
                                                                    const QHash<QString, Calendar> &calendarsById)
{
    QHash<QDate, QList<MonthDayEventItem>> result;
    for (const Event &event : events) {
        MonthDayEventItem item;
        item.eventId = event.id;
        item.calendarId = event.calendarId;
        item.title = event.summary.isEmpty() ? QObject::tr("(No title)") : event.summary;
        item.color = calendarsById.contains(event.calendarId) ? calendarsById.value(event.calendarId).color : QColor(Qt::gray);
        item.allDay = event.allDay;
        if (!event.allDay) {
            item.timeLabel = QLocale::system().toString(event.startDateTime.toLocalTime().time(), QLocale::ShortFormat);
            item.startInstant = event.startDateTime;
            item.endInstant = event.endDateTime;
        }

        // event.endDate is exclusive for all-day events (Google semantics:
        // a 1-day all-day event has endDate == startDate + 1), but inclusive
        // for timed events (it's simply which local day the end instant
        // falls on). Normalize to the last *inclusive* day, then repeat the
        // item on every day the event spans so multi-day events stay
        // visible on each covered day, not just their start date. Capped
        // defensively against malformed/pathological data.
        QDate lastDateInclusive = event.allDay ? event.endDate.addDays(-1) : event.endDate;
        if (lastDateInclusive < event.startDate)
            lastDateInclusive = event.startDate;

        constexpr qint64 kMaxSpanDays = 90;
        const qint64 spanDays = qBound<qint64>(0, event.startDate.daysTo(lastDateInclusive), kMaxSpanDays - 1);

        for (qint64 offset = 0; offset <= spanDays; ++offset)
            result[event.startDate.addDays(offset)].append(item);
    }
    return result;
}
