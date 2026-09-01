#ifndef EVENTPILLLABEL_H
#define EVENTPILLLABEL_H

#include "montheventitem.h"

#include <QLabel>

// One event pill inside a MonthDayCellWidget. Unlike a plain QLabel it is
// NOT transparent for mouse events — it needs to distinguish a single click
// (forward as date selection, same as clicking empty cell space) from a
// double click (open the event for editing). It never swallows the single
// click's effect: the underlying press is always relayed via singleClicked,
// including the first press of a double-click sequence, so double-clicking
// a pill both selects its date AND requests editing, matching the existing
// whole-cell double-click-to-create precedent.
class EventPillLabel : public QLabel
{
    Q_OBJECT
public:
    explicit EventPillLabel(const MonthDayEventItem &item, QWidget *parent = nullptr);

    // True when this pill's event is (calendarId, eventId) and eventId is
    // non-empty — the selection test every owning cell runs over its pills.
    bool matchesEvent(const QString &calendarId, const QString &eventId) const;

    // Draws a highlight outline around the pill (on top of the per-calendar
    // bg/fg fill the ctor applies) — the "this is the selected event" cue
    // for the Delete-key shortcut. Painted in paintEvent() rather than via
    // the stylesheet so it stays independent of the per-calendar colour
    // string; same approach MonthDayCellWidget uses for the selected-day
    // outline.
    void setSelected(bool selected);

signals:
    void singleClicked(const QString &calendarId, const QString &eventId);
    void editRequested(const QString &calendarId, const QString &eventId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_calendarId;
    QString m_eventId;
    bool m_selected = false;
};

#endif // EVENTPILLLABEL_H
