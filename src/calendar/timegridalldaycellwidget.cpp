#include "timegridalldaycellwidget.h"

#include "eventpilllabel.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

TimeGridAllDayCellWidget::TimeGridAllDayCellWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    // Trailing stretch so the pill stack stays pinned to the top when this
    // cell is stretched taller than its content by a busier day elsewhere in
    // the strip (same reason as MonthDayCellWidget's trailing stretch).
    layout->addStretch();

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize TimeGridAllDayCellWidget::minimumSizeHint() const
{
    // Width is a fixed floor, NOT taken from the layout: an all-day pill
    // must never widen its cell (see EventPillLabel's ctor), otherwise this
    // day's column stops matching the timed-grid column directly below it
    // and the all-day strip visibly drifts out of alignment. Height still
    // comes from the layout so the strip grows to fit the busiest day's
    // stack of pills.
    return QSize(20, qMax(layout()->minimumSize().height(), 4));
}

void TimeGridAllDayCellWidget::setDate(const QDate &date)
{
    m_date = date;
}

void TimeGridAllDayCellWidget::setEvents(const QList<MonthDayEventItem> &events)
{
    m_events.clear();
    for (const MonthDayEventItem &item : events) {
        if (item.allDay)
            m_events.append(item);
    }
    rebuildEventWidgets();
}

void TimeGridAllDayCellWidget::mousePressEvent(QMouseEvent *event)
{
    // Deliberately not forwarded to QWidget::mousePressEvent(): its default
    // mouseDoubleClickEvent() implementation calls mousePressEvent() again
    // on the second click, which would re-emit clicked() a second time for
    // the same gesture (same precedent as MonthDayCellWidget).
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_date);
        event->accept();
    }
}

void TimeGridAllDayCellWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(m_date);
        event->accept();
    }
}

void TimeGridAllDayCellWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Same rebuild-avoidance reasoning as MonthDayCellWidget::resizeEvent().
    if (!m_eventWidgets.isEmpty())
        updateEventPillTexts();
}

void TimeGridAllDayCellWidget::updateEventPillTexts()
{
    for (int i = 0; i < m_eventWidgets.size(); ++i) {
        const MonthDayEventItem &item = m_events.at(i);
        auto *pill = qobject_cast<EventPillLabel *>(m_eventWidgets.at(i));
        const QFontMetrics metrics(pill->font());
        pill->setText(metrics.elidedText(item.title, Qt::ElideRight, qMax(width() - 8, 20)));
    }
}

void TimeGridAllDayCellWidget::rebuildEventWidgets()
{
    auto *vbox = static_cast<QVBoxLayout *>(layout());

    for (QWidget *widget : std::as_const(m_eventWidgets)) {
        vbox->removeWidget(widget);
        widget->deleteLater();
    }
    m_eventWidgets.clear();

    int insertIndex = 0; // before the trailing stretch
    for (const MonthDayEventItem &item : std::as_const(m_events)) {
        auto *pill = new EventPillLabel(item, this);
        pill->setToolTip(item.title);
        connect(pill, &EventPillLabel::singleClicked, this, [this] { emit clicked(m_date); });
        connect(pill, &EventPillLabel::editRequested, this, &TimeGridAllDayCellWidget::eventEditRequested);

        const QColor bg = item.color.isValid() ? item.color : QColor(Qt::gray);
        const QColor fg = bg.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
        pill->setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 3px; padding: 1px 3px; }")
                                 .arg(bg.name(), fg.name()));

        const QFontMetrics metrics(pill->font());
        pill->setText(metrics.elidedText(item.title, Qt::ElideRight, qMax(width() - 8, 20)));

        vbox->insertWidget(insertIndex, pill);
        m_eventWidgets.append(pill);
        ++insertIndex;
    }

    updateGeometry();
}
