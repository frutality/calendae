#include "timegriddaycolumnwidget.h"

#include "eventpilllabel.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTime>
#include <QTimeZone>
#include <algorithm>

namespace {

struct LayoutItem
{
    MonthDayEventItem event;
    QDateTime clippedStart;
    QDateTime clippedEnd;
    int columnIndex = 0;
    int columnCount = 1;
};

// Greedy interval-partitioning: assigns each event in [begin, end) (already
// sorted by clippedStart) the first side-by-side column whose last-placed
// event ends at or before this one's start, opening a new column otherwise.
// All events in this range are assumed to belong to one mutually-connected
// overlap cluster, so they share the same columnCount (final width divisor).
void assignColumnsForCluster(QList<LayoutItem> &items, int begin, int end)
{
    QList<QDateTime> columnEnds;
    for (int i = begin; i < end; ++i) {
        int chosenColumn = -1;
        for (int c = 0; c < columnEnds.size(); ++c) {
            if (columnEnds[c] <= items[i].clippedStart) {
                chosenColumn = c;
                break;
            }
        }
        if (chosenColumn < 0) {
            chosenColumn = columnEnds.size();
            columnEnds.append(items[i].clippedEnd);
        } else {
            columnEnds[chosenColumn] = items[i].clippedEnd;
        }
        items[i].columnIndex = chosenColumn;
    }
    for (int i = begin; i < end; ++i)
        items[i].columnCount = columnEnds.size();
}

} // namespace

TimeGridDayColumnWidget::TimeGridDayColumnWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kDayHeight);
}

void TimeGridDayColumnWidget::setDate(const QDate &date)
{
    if (m_date == date)
        return;
    m_date = date;
    update();
}

void TimeGridDayColumnWidget::setIsToday(bool isToday)
{
    if (m_isToday == isToday)
        return;
    m_isToday = isToday;
    update();
}

void TimeGridDayColumnWidget::setEvents(const QList<MonthDayEventItem> &events)
{
    m_events = events;
    relayoutEvents();
}

void TimeGridDayColumnWidget::refreshNowLine()
{
    if (m_isToday)
        update();
}

void TimeGridDayColumnWidget::relayoutEvents()
{
    for (QWidget *widget : std::as_const(m_eventWidgets))
        widget->deleteLater();
    m_eventWidgets.clear();

    if (!m_date.isValid())
        return;

    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    const QDateTime dayStart(m_date, QTime(0, 0), localTimeZone);
    const QDateTime dayEnd = dayStart.addDays(1);

    QList<LayoutItem> items;
    for (const MonthDayEventItem &event : std::as_const(m_events)) {
        if (event.allDay || !event.startInstant.isValid() || !event.endInstant.isValid())
            continue;
        const QDateTime clippedStart = qMax(event.startInstant, dayStart);
        const QDateTime clippedEnd = qMin(event.endInstant, dayEnd);
        if (clippedEnd <= clippedStart)
            continue;
        items.append(LayoutItem{event, clippedStart, clippedEnd, 0, 1});
    }
    if (items.isEmpty())
        return;

    std::stable_sort(items.begin(), items.end(), [](const LayoutItem &a, const LayoutItem &b) {
        if (a.clippedStart != b.clippedStart)
            return a.clippedStart < b.clippedStart;
        return a.clippedEnd < b.clippedEnd;
    });

    // Partition into clusters of mutually-overlapping events (a run where
    // each event starts before the running max end of everything seen so
    // far in the cluster), then assign side-by-side columns within each.
    int clusterBegin = 0;
    QDateTime clusterEnd = items[0].clippedEnd;
    for (int i = 1; i < items.size(); ++i) {
        if (items[i].clippedStart < clusterEnd) {
            clusterEnd = qMax(clusterEnd, items[i].clippedEnd);
            continue;
        }
        assignColumnsForCluster(items, clusterBegin, i);
        clusterBegin = i;
        clusterEnd = items[i].clippedEnd;
    }
    assignColumnsForCluster(items, clusterBegin, items.size());

    const int usableWidth = qMax(width(), 1);
    constexpr int kGap = 2;

    // The pill's stylesheet adds 1px top/bottom padding (see below); a box
    // shorter than the font's own line height plus that padding clips the
    // text instead of just looking tight, which is what made short (e.g.
    // 15-minute) events render with their descenders cut off. Month view
    // never hits this because its pills are sized by QLabel's own
    // sizeHint() (no explicit height); here the box height is driven by
    // event duration, so it must be floored at whatever one line actually
    // needs, not just the fixed kMinEventHeight.
    const int minPillHeight = qMax(kMinEventHeight, QFontMetrics(font()).height() + 4);

    for (const LayoutItem &layoutItem : std::as_const(items)) {
        const double minutesFromMidnight = dayStart.secsTo(layoutItem.clippedStart) / 60.0;
        const double durationMinutes = layoutItem.clippedStart.secsTo(layoutItem.clippedEnd) / 60.0;
        const int y = qRound(minutesFromMidnight * (kSlotHeight / 30.0));
        const int h = qMax(minPillHeight, qRound(durationMinutes * (kSlotHeight / 30.0)));

        const int columnWidth = usableWidth / layoutItem.columnCount;
        const int x = layoutItem.columnIndex * columnWidth;
        const int w = qMax(columnWidth - kGap, 10);

        const MonthDayEventItem &item = layoutItem.event;
        auto *pill = new EventPillLabel(item, this);
        pill->setToolTip(QStringLiteral("%1 %2").arg(item.timeLabel, item.title));
        connect(pill, &EventPillLabel::singleClicked, this, [this] { emit clicked(m_date); });
        connect(pill, &EventPillLabel::editRequested, this, &TimeGridDayColumnWidget::eventEditRequested);

        const QColor bg = item.color.isValid() ? item.color : QColor(Qt::gray);
        const QColor fg = bg.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
        pill->setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 3px; padding: 1px 3px; }")
                                 .arg(bg.name(), fg.name()));
        // Deliberately no explicit setAlignment(): QLabel's default
        // (AlignLeft | AlignVCenter) is what MonthDayCellWidget's pills rely
        // on too, and now that h is always tall enough for one line, that's
        // enough to keep text centered without clipping.
        pill->setWordWrap(false);

        const QFontMetrics metrics(pill->font());
        pill->setText(metrics.elidedText(item.title, Qt::ElideRight, qMax(w - 6, 20)));

        pill->setGeometry(x, y, w, h);
        pill->show();
        m_eventWidgets.append(pill);
    }
}

QDateTime TimeGridDayColumnWidget::slotStartForY(int y) const
{
    const int clampedY = qBound(0, y, kDayHeight - 1);
    const int totalMinutes = qRound(clampedY / (kSlotHeight / 30.0));
    const int slotMinutes = (totalMinutes / 30) * 30; // snap down to the containing half-hour
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    return QDateTime(m_date, QTime(0, 0), localTimeZone).addSecs(slotMinutes * 60);
}

void TimeGridDayColumnWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    if (m_isToday) {
        QColor tint = palette().color(QPalette::Highlight);
        tint.setAlpha(18);
        painter.fillRect(rect(), tint);
    }

    const QColor hourLineColor = palette().color(QPalette::Mid);
    QColor halfHourLineColor = hourLineColor;
    halfHourLineColor.setAlpha(90);

    for (int slot = 0; slot < kSlotsPerDay; ++slot) {
        const int y = slot * kSlotHeight;
        painter.setPen((slot % 2 == 0) ? hourLineColor : halfHourLineColor);
        painter.drawLine(0, y, width(), y);
    }

    if (m_isToday) {
        const QTime now = QTime::currentTime();
        const int y = qRound((now.hour() * 60 + now.minute()) * (kSlotHeight / 30.0));
        QPen pen(QColor(220, 40, 40));
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawLine(0, y, width(), y);
    }
}

void TimeGridDayColumnWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_date);
        event->accept();
    }
}

void TimeGridDayColumnWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit slotDoubleClicked(slotStartForY(event->pos().y()));
        event->accept();
    }
}

void TimeGridDayColumnWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Unlike MonthDayCellWidget, this widget's height is fixed (kDayHeight)
    // and never depends on its own content, so a width-driven relayout here
    // can't trigger the resize->rebuild->resize convergence issue that
    // forced MonthDayCellWidget to only re-elide text on resize instead of
    // rebuilding — a full reposition/re-elide on every resize is safe here.
    if (!m_eventWidgets.isEmpty())
        relayoutEvents();
}
