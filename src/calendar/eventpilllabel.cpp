#include "eventpilllabel.h"

#include <QMouseEvent>

EventPillLabel::EventPillLabel(const MonthDayEventItem &item, QWidget *parent)
    : QLabel(parent)
    , m_calendarId(item.calendarId)
    , m_eventId(item.eventId)
{
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
