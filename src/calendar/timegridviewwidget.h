#ifndef TIMEGRIDVIEWWIDGET_H
#define TIMEGRIDVIEWWIDGET_H

#include "montheventitem.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class TimeGridViewWidget;
}
class QScrollArea;
class QToolButton;
class QTimer;
QT_END_NAMESPACE

class TimeGridAllDayCellWidget;
class TimeGridDayColumnWidget;

// The week/day time-grid view: dayCount columns (7 for week, 1 for day),
// each split into half-hour rows with timed events positioned by clock
// time (overlapping events laid out side-by-side) and all-day events
// pinned to a strip above the scrollable grid. Carries no event data of
// its own — TimeGridEventsController pushes it in per-calendar, same
// contract as MonthViewWidget.
class TimeGridViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimeGridViewWidget(int dayCount, QWidget *parent = nullptr); // 1 = day, 7 = week
    ~TimeGridViewWidget() override;

    int dayCount() const { return m_dayCount; }
    QDate selectedDate() const { return m_selectedDate; }
    QDate rangeStart() const { return m_rangeStart; } // Monday of the week (week mode) or the day itself (day mode)

    // Scroll position of the half-hour grid, in pixels — x (horizontal) is
    // always 0 (this grid never scrolls horizontally, see
    // Qt::ScrollBarAlwaysOff in the constructor), kept as a QPoint only for
    // a uniform interface with MonthViewWidget's grid, which does scroll
    // both ways. Exposed so MainWindow can persist/restore it across
    // restarts alongside window geometry and the last-active view.
    QPoint scrollPosition() const;
    void setScrollPosition(const QPoint &value);

    // Current maximum scrollable value on each axis — the vertical one can
    // shift slightly as the all-day strip grows with real event content,
    // shrinking the scroll viewport. MainWindow polls this to detect when
    // the view has actually finished settling before treating a restored
    // scroll position as final.
    QPoint maxScrollPosition() const;

public slots:
    void goToPrevious();
    void goToNext();
    void goToToday();
    void selectDate(const QDate &date);

    void setEventsForCalendar(const QString &calendarId, const QHash<QDate, QList<MonthDayEventItem>> &eventsByDate);
    void clearEventsForCalendar(const QString &calendarId);
    void clearAllEvents();

signals:
    void dateSelected(QDate date);
    void displayedRangeChanged(QDate rangeStart);
    void newEventRequested(QDate date); // all-day row / header click-to-create
    void newTimedEventRequested(QDateTime startDateTime); // half-hour slot double-click
    void eventEditRequested(const QString &calendarId, const QString &eventId);

private:
    void refreshColumns();
    void updateRangeLabel();
    void updateHeaderAndColumnStates();
    void rebuildAllCellEventLists();
    void onColumnClicked(QDate date);

    Ui::TimeGridViewWidget *ui;
    int m_dayCount;
    QDate m_rangeStart;
    QDate m_selectedDate;

    QVector<QToolButton *> m_headerButtons; // dayCount, one per column
    QVector<TimeGridAllDayCellWidget *> m_allDayCells; // dayCount
    QVector<TimeGridDayColumnWidget *> m_dayColumns; // dayCount
    QScrollArea *m_scrollArea;
    QTimer *m_nowLineTimer;

    QHash<QString, QHash<QDate, QList<MonthDayEventItem>>> m_eventsByCalendar;
};

#endif // TIMEGRIDVIEWWIDGET_H
