#include "monthdaycellwidget.h"

#include "eventpilllabel.h"

#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
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

void MonthDayCellWidget::mousePressEvent(QMouseEvent *event)
{
    // Deliberately not forwarded to QWidget::mousePressEvent(): its default
    // mouseDoubleClickEvent() implementation calls mousePressEvent() again
    // on the second click, which would re-emit clicked() a second time for
    // the same gesture. Accepting here and stopping avoids that.
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_date);
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
        connect(pill, &EventPillLabel::singleClicked, this, [this] { emit clicked(m_date); });
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

    updateGeometry();
}

void MonthDayCellWidget::updateVisualState()
{
    m_dayNumberLabel->setText(m_date.isValid() ? QString::number(m_date.day()) : QString());

    if (m_selected) {
        setStyleSheet(QStringLiteral("MonthDayCellWidget { background-color: palette(highlight); border-radius: 4px; }"));
        m_dayNumberLabel->setStyleSheet(QStringLiteral("color: palette(highlighted-text); font-weight: bold;"));
    } else if (m_isToday) {
        setStyleSheet(QStringLiteral("MonthDayCellWidget { border: 2px solid palette(highlight); border-radius: 4px; }"));
        m_dayNumberLabel->setStyleSheet(m_inCurrentMonth
            ? QStringLiteral("font-weight: bold;")
            : QStringLiteral("color: palette(mid); font-weight: bold;"));
    } else {
        setStyleSheet(QString());
        m_dayNumberLabel->setStyleSheet(m_inCurrentMonth ? QString() : QStringLiteral("color: palette(mid);"));
    }
}
