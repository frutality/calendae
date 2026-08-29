#include "eventpilllabel.h"

#include <QMouseEvent>
#include <QSizePolicy>

EventPillLabel::EventPillLabel(const MonthDayEventItem &item, QWidget *parent)
    : QLabel(parent)
    , m_calendarId(item.calendarId)
    , m_eventId(item.eventId)
{
    // A plain non-wrapping QLabel reports its full (elided) text width as
    // both its sizeHint and its minimumSizeHint, so every layout it sits in
    // inherits a per-item minimum width that grows with the event title.
    // In the month grid and the all-day strip that makes day columns take
    // unequal widths (stretch only divides space *above* each column's
    // minimum) and re-flow every time a calendar streams in with
    // longer/shorter titles. Ignored horizontally means "impose no width;
    // take whatever the column gives" — the elidedText() calls in the
    // owning cell already handle overflow. Height is untouched, so a busy
    // day still grows taller. Harmless where a pill is positioned by
    // setGeometry() (TimeGridDayColumnWidget) rather than by a layout.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(0);
}

void EventPillLabel::mousePressEvent(QMouseEvent *event)
{
    // Deliberately do NOT forward to QLabel::mousePressEvent(): QWidget's
    // default mouseDoubleClickEvent() implementation calls mousePressEvent()
    // on the second click, and an event left unaccepted also bubbles up to
    // the parent cell — either would cause this single click to be
    // re-processed a second time (or reach the cell at all). Calling
    // accept() and stopping here avoids both.
    if (event->button() == Qt::LeftButton) {
        emit singleClicked();
        event->accept();
    }
}

void EventPillLabel::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Same reasoning as mousePressEvent(): fully handle and accept here,
    // don't forward to the base class, so this double click can't also
    // bubble up and be reinterpreted as a double click on the parent cell
    // (which would incorrectly open the "create event" dialog).
    if (event->button() == Qt::LeftButton) {
        emit editRequested(m_calendarId, m_eventId);
        event->accept();
    }
}
