#include "monthdaycellwidget.h"

#include "eventpilllabel.h"

#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

MonthDayCellWidget::MonthDayCellWidget(QWidget *parent)
    : QWidget(parent)
    , m_dayNumberLabel(new QLabel(this))
{
    // Labels must not swallow clicks meant for the cell itself — otherwise
    // most of a busy cell (covered in event pills) becomes unclickable.
    m_dayNumberLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->addWidget(m_dayNumberLabel, 0, Qt::AlignTop | Qt::AlignRight);
    layout->addStretch();

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    updateVisualState();
}

QSize MonthDayCellWidget::minimumSizeHint() const
{
    // Width is deliberately a fixed floor, NOT taken from the layout: the
    // event pills must never widen a cell (see EventPillLabel's ctor), so
    // that all 7 grid columns stay equal-width and content-independent —
    // the equal column stretch factors then divide the row evenly. On a
    // window too narrow to give every column this floor, the grid keeps the
    // floor and its scroll area shows a horizontal scrollbar rather than
    // crushing the cells. Height still comes from the layout so a day with
    // many stacked pills grows taller (see the header-file note on why this
    // isn't a fixed setMinimumSize()).
    return QSize(kMinContentWidth, qMax(layout()->minimumSize().height(), 60));
}

void MonthDayCellWidget::setDate(const QDate &date)
{
    if (m_date == date)
        return;
    m_date = date;
    updateVisualState();
}

void MonthDayCellWidget::setInCurrentMonth(bool inCurrentMonth)
{
    if (m_inCurrentMonth == inCurrentMonth)
        return;
    m_inCurrentMonth = inCurrentMonth;
    updateVisualState();
}

void MonthDayCellWidget::setIsToday(bool isToday)
{
    if (m_isToday == isToday)
        return;
    m_isToday = isToday;
    updateVisualState();
}

void MonthDayCellWidget::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    updateVisualState();
}

void MonthDayCellWidget::setEvents(const QList<MonthDayEventItem> &events)
{
    m_events = events;
    rebuildEventWidgets();
}

void MonthDayCellWidget::setSelectedEvent(const QString &calendarId, const QString &eventId)
{
    if (m_selectedCalendarId == calendarId && m_selectedEventId == eventId)
        return;
    m_selectedCalendarId = calendarId;
    m_selectedEventId = eventId;
    applyEventSelection();
}

void MonthDayCellWidget::applyEventSelection()
{
    for (int i = 0; i < m_eventWidgets.size(); ++i) {
        const MonthDayEventItem &item = m_events.at(i);
        const bool selected = !m_selectedEventId.isEmpty() && item.eventId == m_selectedEventId
            && item.calendarId == m_selectedCalendarId;
        qobject_cast<EventPillLabel *>(m_eventWidgets.at(i))->setSelected(selected);
    }
}

void MonthDayCellWidget::mousePressEvent(QMouseEvent *event)
{
    // Deliberately not forwarded to QWidget::mousePressEvent(): its default
    // mouseDoubleClickEvent() implementation calls mousePressEvent() again
    // on the second click, which would re-emit clicked() a second time for
    // the same gesture. Accepting here and stopping avoids that.
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_date);
        emit backgroundClicked(m_date);
        event->accept();
    }
}

void MonthDayCellWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(m_date);
        event->accept();
    }
}

void MonthDayCellWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    // All state backgrounds are painted here rather than via setStyleSheet():
    // a plain QWidget subclass needs Qt::WA_StyledBackground for a stylesheet
    // "background-color" to render at all, and even with it a translucent wash
    // on top of the (highlight-coloured) selection fill would be invisible.
    //
    // So the two cues are kept visually independent, and both show at once
    // (today is the selected day on every fresh launch):
    //   - today    -> a translucent highlight wash, matching the week view
    //                 (TimeGridDayColumnWidget::paintEvent).
    //   - selected -> a 2px highlight outline.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor highlight = palette().color(QPalette::Highlight);

    if (m_isToday) {
        QColor wash = highlight;
        wash.setAlpha(45);
        painter.setPen(Qt::NoPen);
        painter.setBrush(wash);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }

    if (m_selected) {
        painter.setPen(QPen(highlight, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
    }
}

void MonthDayCellWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Deliberately does NOT call rebuildEventWidgets(): that tears down and
    // recreates every pill (removeWidget()+insertWidget() on the vbox for
    // each one), and once a cell's height can legitimately grow with its
    // content (see minimumSizeHint() above), populating events triggers a
    // cascade of resize->rebuild->resize passes as the grid/scroll area
    // converge on a stable size. Rebuilding the whole widget list on every
    // one of those passes left some newly-inserted pills stranded before
    // ever getting a layout position (all stacked at (0,0), overlapping the
    // day number) -- confirmed via an instrumented repro that logged 4
    // full rebuild cycles for a single setEventsForCalendar() call, with
    // only the first few pills of the final cycle ever laid out. Only the
    // text needs to change on a resize (re-eliding for the new width); the
    // widget list itself only changes when the event set changes.
    if (!m_eventWidgets.isEmpty())
        updateEventPillTexts();
}

void MonthDayCellWidget::updateEventPillTexts()
{
    for (int i = 0; i < m_eventWidgets.size(); ++i) {
        const MonthDayEventItem &item = m_events.at(i);
        const QString rawText = item.allDay
            ? item.title
            : QStringLiteral("%1 %2").arg(item.timeLabel, item.title);

        auto *pill = qobject_cast<EventPillLabel *>(m_eventWidgets.at(i));
        const QFontMetrics metrics(pill->font());
        pill->setText(metrics.elidedText(rawText, Qt::ElideRight, qMax(width() - 12, 20)));
    }
}

void MonthDayCellWidget::rebuildEventWidgets()
{
    auto *vbox = static_cast<QVBoxLayout *>(layout());

    for (QWidget *widget : std::as_const(m_eventWidgets)) {
        vbox->removeWidget(widget);
        widget->deleteLater();
    }
    m_eventWidgets.clear();

    int insertIndex = 1; // after the day-number label, before the trailing stretch
    for (const MonthDayEventItem &item : std::as_const(m_events)) {
        const QString rawText = item.allDay
            ? item.title
            : QStringLiteral("%1 %2").arg(item.timeLabel, item.title);

        auto *pill = new EventPillLabel(item, this);
        pill->setToolTip(rawText);
        connect(pill, &EventPillLabel::singleClicked, this, [this](const QString &calendarId, const QString &eventId) {
            emit clicked(m_date);
            emit eventClicked(calendarId, eventId);
        });
        connect(pill, &EventPillLabel::editRequested, this, &MonthDayCellWidget::eventEditRequested);

        const QColor bg = item.color.isValid() ? item.color : QColor(Qt::gray);
        const QColor fg = bg.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
        pill->setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 3px; padding: 1px 3px; }")
                                 .arg(bg.name(), fg.name()));

        const QFontMetrics metrics(pill->font());
        pill->setText(metrics.elidedText(rawText, Qt::ElideRight, qMax(width() - 12, 20)));

        vbox->insertWidget(insertIndex, pill);
        m_eventWidgets.append(pill);
        ++insertIndex;
    }

    applyEventSelection();
    updateGeometry();
}

void MonthDayCellWidget::updateVisualState()
{
    m_dayNumberLabel->setText(m_date.isValid() ? QString::number(m_date.day()) : QString());

    // The selected / today backgrounds are drawn in paintEvent(); here we only
    // set the day-number emphasis. Bold marks both selected and today.
    if (m_selected || m_isToday) {
        m_dayNumberLabel->setStyleSheet(m_inCurrentMonth
            ? QStringLiteral("font-weight: bold;")
            : QStringLiteral("color: palette(mid); font-weight: bold;"));
    } else {
        m_dayNumberLabel->setStyleSheet(m_inCurrentMonth ? QString() : QStringLiteral("color: palette(mid);"));
    }

    update();
}
