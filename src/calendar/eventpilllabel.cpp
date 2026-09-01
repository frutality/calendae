#include "eventpilllabel.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
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

    // Per-calendar fill with a foreground kept legible against it. Every
    // cell type applied this same string itself before.
    const QColor bg = item.color.isValid() ? item.color : QColor(Qt::gray);
    const QColor fg = bg.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 3px; padding: 1px 3px; }")
                      .arg(bg.name(), fg.name()));
}

bool EventPillLabel::matchesEvent(const QString &calendarId, const QString &eventId) const
{
    return !eventId.isEmpty() && eventId == m_eventId && calendarId == m_calendarId;
}

void EventPillLabel::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
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
        emit singleClicked(m_calendarId, m_eventId);
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

void EventPillLabel::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);

    if (!m_selected)
        return;

    // A 2px highlight outline inset by 1px, radius matching the pill's own
    // "border-radius: 3px" stylesheet. Drawn after the base class so it
    // sits on top of the calendar-coloured background.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 3, 3);
}
