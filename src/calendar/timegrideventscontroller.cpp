#include "timegrideventscontroller.h"

#include "monthkeys.h"
#include "timegridviewwidget.h"

TimeGridEventsController::TimeGridEventsController(AuthManager *authManager, MonthEventStore *store,
                                                     TimeGridViewWidget *view, QObject *parent)
    : StoreBackedEventsController(authManager, store, view, parent)
    , m_timeGridView(view)
{
    connect(m_timeGridView, &TimeGridViewWidget::displayedRangeChanged,
            this, &TimeGridEventsController::onDisplayedRangeChanged);
}

QList<QDate> TimeGridEventsController::currentMonthKeys() const
{
    const QDate start = m_timeGridView->rangeStart();
    if (!start.isValid())
        return {};
    return MonthKeys::forDateRange(start, start.addDays(m_timeGridView->dayCount() - 1));
}

void TimeGridEventsController::onDisplayedRangeChanged(const QDate &rangeStart)
{
    Q_UNUSED(rangeStart);
    reloadCurrentRange();
}
