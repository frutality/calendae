#ifndef TIMEGRIDDAYCOLUMNWIDGET_H
#define TIMEGRIDDAYCOLUMNWIDGET_H

#include "montheventitem.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

// One day's column inside the time-grid (week/day) view's scrollable body:
// a fixed-height, half-hour-ruled track that positions timed events by
// clock time, with overlapping events split side-by-side (interval-
// partitioning, same idea a real calendar app uses). All-day events are
// NOT shown here — those live in TimeGridAllDayCellWidget, pinned above
// the scroll area. Built entirely in code (no .ui), same as
// MonthDayCellWidget.
class TimeGridDayColumnWidget : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kSlotHeight = 30; // px per half-hour row
    static constexpr int kSlotsPerDay = 48;
    static constexpr int kDayHeight = kSlotHeight * kSlotsPerDay;
    static constexpr int kMinEventHeight = 18; // px; keeps very short events clickable

    explicit TimeGridDayColumnWidget(QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate date() const { return m_date; }

    void setIsToday(bool isToday);

public slots:
    // events may include allDay items (from a merged per-date list) — those
    // are silently ignored here; only timed (allDay == false) items with
    // valid startInstant/endInstant are laid out.
    void setEvents(const QList<MonthDayEventItem> &events);

    // Marks the pill for (calendarId, eventId) as selected (empty ids
    // clear). Re-applied across every relayoutEvents() so the highlight
    // survives a resize / refresh.
    void setSelectedEvent(const QString &calendarId, const QString &eventId);

    // Repaints the current-time indicator line at its live position.
    // Cheap no-op when date() isn't today.
    void refreshNowLine();

signals:
    void clicked(QDate date);
    // Emitted only for a press on the column's empty area, not on an event
    // pill (which also emits clicked, for date selection).
    void backgroundClicked(QDate date);
    void slotDoubleClicked(const QDateTime &startDateTime);
    void eventClicked(const QString &calendarId, const QString &eventId);
    void eventEditRequested(const QString &calendarId, const QString &eventId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayoutEvents();
    QDateTime slotStartForY(int y) const;
    int nowLineY() const;
    void updateNowLineOverlay();

    QDate m_date;
    bool m_isToday = false;
    QList<MonthDayEventItem> m_events;
    QList<QWidget *> m_eventWidgets; // EventPillLabel*, absolutely positioned
    // (calendarId, eventId) parallel to m_eventWidgets — m_eventWidgets is a
    // filtered/sorted subset of m_events (timed only), so this maps each
    // pill back to its event for the selection highlight.
    QList<QPair<QString, QString>> m_eventWidgetKeys;
    QString m_selectedCalendarId;
    QString m_selectedEventId;
    // Draws the current-time indicator. A separate top-most child rather
    // than part of paintEvent() so the line stays visible over event pills
    // (which are child widgets and would otherwise paint on top of it).
    QWidget *m_nowLineOverlay = nullptr;
};

#endif // TIMEGRIDDAYCOLUMNWIDGET_H
