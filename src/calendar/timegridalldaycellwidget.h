#ifndef TIMEGRIDALLDAYCELLWIDGET_H
#define TIMEGRIDALLDAYCELLWIDGET_H

#include "montheventitem.h"

#include <QDate>
#include <QList>
#include <QWidget>

// One day's cell in the time-grid view's all-day strip, pinned above the
// scrollable half-hour grid. Same stacked-pill composition as
// MonthDayCellWidget, minus the day-number label (the column header above
// this row already shows the date) and minus the growing-minimum-size
// behavior (this row's height is driven by the tallest cell across the
// whole strip, computed by the parent).
class TimeGridAllDayCellWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimeGridAllDayCellWidget(QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate date() const { return m_date; }

    QSize minimumSizeHint() const override;

public slots:
    // events may include timed items — those are silently ignored; only
    // allDay == true items are shown.
    void setEvents(const QList<MonthDayEventItem> &events);

    // Marks the pill for (calendarId, eventId) as selected (empty ids
    // clear). Re-applied across rebuildEventWidgets().
    void setSelectedEvent(const QString &calendarId, const QString &eventId);

signals:
    void clicked(QDate date);
    // Emitted only for a press on the cell's empty area, not on an event
    // pill (which also emits clicked, for date selection).
    void backgroundClicked(QDate date);
    void doubleClicked(QDate date);
    void eventClicked(const QString &calendarId, const QString &eventId);
    void eventEditRequested(const QString &calendarId, const QString &eventId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildEventWidgets();
    void updateEventPillTexts();
    void applyEventSelection();

    QList<QWidget *> m_eventWidgets;
    QList<MonthDayEventItem> m_events; // allDay items only, filtered in setEvents
    QString m_selectedCalendarId;
    QString m_selectedEventId;
    QDate m_date;
};

#endif // TIMEGRIDALLDAYCELLWIDGET_H
