#ifndef MONTHVIEWWIDGET_H
#define MONTHVIEWWIDGET_H

#include "montheventitem.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class MonthViewWidget;
}
QT_END_NAMESPACE

class MonthDayCellWidget;

// The month-view grid: navigable and selectable, but carries no event data
// of its own (that's wired in by a later milestone via dateSelected /
// displayedMonthChanged).
class MonthViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MonthViewWidget(QWidget *parent = nullptr);
    ~MonthViewWidget() override;

    QDate selectedDate() const { return m_selectedDate; }
    QDate displayedMonth() const { return m_displayedMonth; } // always first-of-month

public slots:
    void goToPreviousMonth();
    void goToNextMonth();
    void goToToday();
    void selectDate(const QDate &date);

    void setEventsForCalendar(const QString &calendarId, const QHash<QDate, QList<MonthDayEventItem>> &eventsByDate);
    void clearEventsForCalendar(const QString &calendarId);
    void clearAllEvents();

signals:
    void dateSelected(QDate date);
    void displayedMonthChanged(QDate firstOfMonth);
    void newEventRequested(QDate date);
    void eventEditRequested(const QString &calendarId, const QString &eventId);

private:
    void buildWeekdayHeader();
    void refreshCells();
    void updateMonthYearLabel();
    void updateCellStates();
    void onCellClicked(QDate date);
    void rebuildAllCellEventLists();

    Ui::MonthViewWidget *ui;
    QDate m_displayedMonth;
    QDate m_selectedDate;
    QVector<MonthDayCellWidget *> m_cells; // 42, row-major, created once
    QHash<QDate, MonthDayCellWidget *> m_cellByDate; // rebuilt every refreshCells()
    QHash<QString, QHash<QDate, QList<MonthDayEventItem>>> m_eventsByCalendar;
};

#endif // MONTHVIEWWIDGET_H
