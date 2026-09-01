#include "montheventscontroller.h"

#include "monthkeys.h"
#include "monthviewwidget.h"

MonthEventsController::MonthEventsController(AuthManager *authManager, MonthEventStore *store,
                                               MonthViewWidget *monthView, QObject *parent)
    : StoreBackedEventsController(authManager, store, monthView, parent)
    , m_monthView(monthView)
{
    connect(m_monthView, &MonthViewWidget::displayedMonthChanged,
            this, &MonthEventsController::onDisplayedMonthChanged);
}

QList<QDate> MonthEventsController::currentMonthKeys() const
{
    const QDate month = MonthKeys::normalize(m_monthView->displayedMonth());
    return month.isValid() ? QList<QDate>{month} : QList<QDate>{};
}

void MonthEventsController::onDisplayedMonthChanged(const QDate &firstOfMonth)
{
    Q_UNUSED(firstOfMonth);
    reloadCurrentRange();
}
