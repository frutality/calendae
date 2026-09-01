#ifndef TIMEGRIDEVENTSCONTROLLER_H
#define TIMEGRIDEVENTSCONTROLLER_H

#include "storebackedeventscontroller.h"

#include <QDate>
#include <QList>

class AuthManager;
class MonthEventStore;
class TimeGridViewWidget;

// Store-backed events controller for the week/day time grid. All the
// machinery lives in StoreBackedEventsController; this only maps the
// visible dayCount-day range to the month bucket(s) it touches (one
// usually, two across a month boundary) and wires the view's navigation
// signal. One instance per view — week and day navigate independently.
class TimeGridEventsController : public StoreBackedEventsController
{
    Q_OBJECT
public:
    explicit TimeGridEventsController(AuthManager *authManager, MonthEventStore *store,
                                       TimeGridViewWidget *view, QObject *parent = nullptr);

protected:
    QList<QDate> currentMonthKeys() const override; // month(s) the visible range touches

private slots:
    void onDisplayedRangeChanged(const QDate &rangeStart);

private:
    TimeGridViewWidget *m_timeGridView; // == m_view, kept typed for rangeStart()/dayCount()
};

#endif // TIMEGRIDEVENTSCONTROLLER_H
