#ifndef MONTHEVENTSCONTROLLER_H
#define MONTHEVENTSCONTROLLER_H

#include "storebackedeventscontroller.h"

#include <QDate>
#include <QList>

class AuthManager;
class MonthEventStore;
class MonthViewWidget;

// Store-backed events controller for the 42-day month grid. All the
// machinery lives in StoreBackedEventsController; this only pins the
// visible range to the single displayed month and wires the month view's
// navigation signal.
class MonthEventsController : public StoreBackedEventsController
{
    Q_OBJECT
public:
    explicit MonthEventsController(AuthManager *authManager, MonthEventStore *store,
                                    MonthViewWidget *monthView, QObject *parent = nullptr);

protected:
    // Just the displayed month, NOT every month the 42-day grid touches:
    // the store fetches each month bucket over that month's full 42-day
    // grid range, so one bucket already covers the grid's leading/trailing
    // adjacent-month days. Spanning it across 3 months here would triple the
    // cold month-view load.
    QList<QDate> currentMonthKeys() const override;

private slots:
    void onDisplayedMonthChanged(const QDate &firstOfMonth);

private:
    MonthViewWidget *m_monthView; // == m_view, kept typed for displayedMonth()
};

#endif // MONTHEVENTSCONTROLLER_H
