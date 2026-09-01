#ifndef EVENTGRIDVIEW_H
#define EVENTGRIDVIEW_H

#include "montheventitem.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>

// The slice of a calendar view widget that StoreBackedEventsController
// drives. MonthViewWidget and TimeGridViewWidget each already expose these
// three methods; inheriting this lets one controller base render into
// either without knowing which. A plain interface (no QObject) so a QWidget
// can multiply inherit it.
class EventGridView
{
public:
    virtual ~EventGridView() = default;

    virtual void setEventsForCalendar(const QString &calendarId,
                                       const QHash<QDate, QList<MonthDayEventItem>> &eventsByDate) = 0;
    virtual void clearEventsForCalendar(const QString &calendarId) = 0;
    virtual void clearAllEvents() = 0;
};

#endif // EVENTGRIDVIEW_H
