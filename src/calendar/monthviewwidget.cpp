#include "monthviewwidget.h"
#include "ui_monthviewwidget.h"

#include "eventgrouping.h"
#include "monthdaycellwidget.h"
#include "monthgrid.h"

#include <QFont>
#include <QLabel>
#include <QLocale>
#include <QScrollBar>
#include <QTimer>
#include <algorithm>

MonthViewWidget::MonthViewWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MonthViewWidget)
{
    ui->setupUi(this);

    // Horizontal scrollbar is left on AsNeeded (the .ui default): a window
    // wide enough for 7 columns of at least MonthDayCellWidget::
    // kMinContentWidth divides the row into 7 equal columns with no
    // scrollbar; a narrower window keeps that per-column floor and scrolls
    // horizontally instead of crushing the cells to unreadable slivers.
    buildWeekdayHeader();

    m_cells.reserve(42);
    for (int i = 0; i < 42; ++i) {
        auto *cell = new MonthDayCellWidget(this);
        connect(cell, &MonthDayCellWidget::clicked, this, &MonthViewWidget::onCellClicked);
        connect(cell, &MonthDayCellWidget::backgroundClicked, this, [this] { clearEventSelection(); });
        connect(cell, &MonthDayCellWidget::doubleClicked, this, &MonthViewWidget::newEventRequested);
        connect(cell, &MonthDayCellWidget::eventClicked, this, &MonthViewWidget::onEventClicked);
        connect(cell, &MonthDayCellWidget::eventEditRequested, this, &MonthViewWidget::eventEditRequested);
        ui->daysGridLayout->addWidget(cell, 1 + i / 7, i % 7);
        m_cells.append(cell);
    }
    for (int col = 0; col < 7; ++col)
        ui->daysGridLayout->setColumnStretch(col, 1);
    for (int row = 1; row <= 6; ++row)
        ui->daysGridLayout->setRowStretch(row, 1);

    connect(ui->prevMonthButton, &QToolButton::clicked, this, &MonthViewWidget::goToPreviousMonth);
    connect(ui->nextMonthButton, &QToolButton::clicked, this, &MonthViewWidget::goToNextMonth);
    connect(ui->todayButton, &QPushButton::clicked, this, &MonthViewWidget::goToToday);
    connect(ui->newEventButton, &QPushButton::clicked, this, [this] { emit newEventRequested(m_selectedDate); });

    const QDate today = QDate::currentDate();
    m_displayedMonth = QDate(today.year(), today.month(), 1);
    m_selectedDate = today;
    m_lastSeenDate = today;

    refreshCells();
    updateMonthYearLabel();
    updateCellStates();

    // With the app left open past local midnight, nothing else recomputes
    // which cell is "today". Poll once a minute and re-mark on a rollover
    // (the displayed month is deliberately NOT navigated).
    auto *dayRolloverTimer = new QTimer(this);
    connect(dayRolloverTimer, &QTimer::timeout, this, [this] {
        const QDate now = QDate::currentDate();
        if (m_lastSeenDate == now)
            return;
        m_lastSeenDate = now;
        updateCellStates();
    });
    dayRolloverTimer->start(60'000);
}

MonthViewWidget::~MonthViewWidget()
{
    delete ui;
}

void MonthViewWidget::buildWeekdayHeader()
{
    const Qt::DayOfWeek firstDayOfWeek = QLocale::system().firstDayOfWeek();
    for (int col = 0; col < 7; ++col) {
        const int dow = 1 + ((static_cast<int>(firstDayOfWeek) - 1 + col) % 7);
        auto *label = new QLabel(QLocale::system().dayName(dow, QLocale::ShortFormat), this);
        label->setAlignment(Qt::AlignCenter);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        ui->daysGridLayout->addWidget(label, 0, col);
    }
}

void MonthViewWidget::refreshCells()
{
    // Any grid rebuild is a navigation (month change / today / cross-month
    // selectDate): the previously selected event pill is about to be
    // destroyed, so drop the selection too.
    clearEventSelection();

    // Defensive belt-and-suspenders: the view can never show stale events
    // for the wrong grid, independent of whether a controller remembers to
    // clear them before navigating.
    m_cellByDate.clear();

    const QList<QDate> dates = MonthGrid::datesForGrid(m_displayedMonth, QLocale::system().firstDayOfWeek());
    for (int i = 0; i < m_cells.size(); ++i) {
        const QDate &date = dates.at(i);
        m_cells[i]->setDate(date);
        m_cells[i]->setInCurrentMonth(date.year() == m_displayedMonth.year() && date.month() == m_displayedMonth.month());
        m_cells[i]->setEvents({});
        m_cellByDate.insert(date, m_cells[i]);
    }
}

void MonthViewWidget::setEventsForCalendar(const QString &calendarId, const QHash<QDate, QList<MonthDayEventItem>> &eventsByDate)
{
    m_eventsByCalendar.insert(calendarId, eventsByDate);
    rebuildAllCellEventLists();
}

void MonthViewWidget::clearEventsForCalendar(const QString &calendarId)
{
    if (m_eventsByCalendar.remove(calendarId) > 0)
        rebuildAllCellEventLists();
}

void MonthViewWidget::clearAllEvents()
{
    m_eventsByCalendar.clear();
    rebuildAllCellEventLists();
}

void MonthViewWidget::rebuildAllCellEventLists()
{
    QHash<QDate, QList<MonthDayEventItem>> merged;
    for (auto calendarIt = m_eventsByCalendar.constBegin(); calendarIt != m_eventsByCalendar.constEnd(); ++calendarIt) {
        const QHash<QDate, QList<MonthDayEventItem>> &byDate = calendarIt.value();
        for (auto dateIt = byDate.constBegin(); dateIt != byDate.constEnd(); ++dateIt)
            merged[dateIt.key()].append(dateIt.value());
    }

    for (auto it = merged.begin(); it != merged.end(); ++it) {
        std::stable_sort(it->begin(), it->end(), [](const MonthDayEventItem &a, const MonthDayEventItem &b) {
            if (a.allDay != b.allDay)
                return a.allDay; // all-day events first
            if (a.allDay)
                return false;
            return a.startInstant < b.startInstant;
        });
    }

    for (auto cellIt = m_cellByDate.constBegin(); cellIt != m_cellByDate.constEnd(); ++cellIt)
        cellIt.value()->setEvents(merged.value(cellIt.key()));

    refreshEventSelection();
}

void MonthViewWidget::updateMonthYearLabel()
{
    ui->monthYearLabel->setText(QStringLiteral("%1 %2")
                                     .arg(QLocale::system().standaloneMonthName(m_displayedMonth.month(), QLocale::LongFormat))
                                     .arg(m_displayedMonth.year()));
}

void MonthViewWidget::updateCellStates()
{
    const QDate today = QDate::currentDate();
    for (MonthDayCellWidget *cell : std::as_const(m_cells)) {
        cell->setIsToday(cell->date() == today);
        cell->setSelected(cell->date() == m_selectedDate);
    }
}

void MonthViewWidget::goToPreviousMonth()
{
    m_displayedMonth = m_displayedMonth.addMonths(-1);
    refreshCells();
    updateMonthYearLabel();
    updateCellStates();
    emit displayedMonthChanged(m_displayedMonth);
}

void MonthViewWidget::goToNextMonth()
{
    m_displayedMonth = m_displayedMonth.addMonths(1);
    refreshCells();
    updateMonthYearLabel();
    updateCellStates();
    emit displayedMonthChanged(m_displayedMonth);
}

void MonthViewWidget::goToToday()
{
    const QDate today = QDate::currentDate();
    m_displayedMonth = QDate(today.year(), today.month(), 1);
    m_selectedDate = today;
    refreshCells();
    updateMonthYearLabel();
    updateCellStates();
    emit displayedMonthChanged(m_displayedMonth);
    emit dateSelected(m_selectedDate);
}

void MonthViewWidget::selectDate(const QDate &date)
{
    const bool monthChanged = date.year() != m_displayedMonth.year() || date.month() != m_displayedMonth.month();
    m_selectedDate = date;

    if (monthChanged) {
        m_displayedMonth = QDate(date.year(), date.month(), 1);
        refreshCells();
        updateMonthYearLabel();
    }

    updateCellStates();

    if (monthChanged)
        emit displayedMonthChanged(m_displayedMonth);
    emit dateSelected(m_selectedDate);
}

void MonthViewWidget::onCellClicked(QDate date)
{
    selectDate(date);
}

void MonthViewWidget::onEventClicked(const QString &calendarId, const QString &eventId)
{
    if (m_selectedEventCalendarId == calendarId && m_selectedEventId == eventId)
        return;
    m_selectedEventCalendarId = calendarId;
    m_selectedEventId = eventId;
    for (MonthDayCellWidget *cell : std::as_const(m_cells))
        cell->setSelectedEvent(calendarId, eventId);
    emit eventSelectionChanged(calendarId, eventId);
}

void MonthViewWidget::clearEventSelection()
{
    if (m_selectedEventCalendarId.isEmpty() && m_selectedEventId.isEmpty())
        return;
    m_selectedEventCalendarId.clear();
    m_selectedEventId.clear();
    for (MonthDayCellWidget *cell : std::as_const(m_cells))
        cell->setSelectedEvent(QString(), QString());
    emit eventSelectionChanged(QString(), QString());
}

void MonthViewWidget::refreshEventSelection()
{
    if (m_selectedEventId.isEmpty())
        return;

    // Scoped to the 42 rendered cells (m_cellByDate): a selection can't
    // survive on an event a refresh pushed outside the visible grid, or
    // Delete-Selected would act on an off-screen event. When it's still
    // present, the rebuilt pills are re-highlighted by each cell's own
    // applyEventSelection() (it keeps the ids across a setEvents()).
    if (!EventGrouping::containsEventOnAnyDate(m_eventsByCalendar, m_selectedEventCalendarId,
                                              m_selectedEventId, m_cellByDate.keys())) {
        clearEventSelection();
    }
}

QPoint MonthViewWidget::scrollPosition() const
{
    return QPoint(ui->daysGridScrollArea->horizontalScrollBar()->value(), ui->daysGridScrollArea->verticalScrollBar()->value());
}

void MonthViewWidget::setScrollPosition(const QPoint &value)
{
    ui->daysGridScrollArea->horizontalScrollBar()->setValue(value.x());
    ui->daysGridScrollArea->verticalScrollBar()->setValue(value.y());
}

QPoint MonthViewWidget::maxScrollPosition() const
{
    return QPoint(ui->daysGridScrollArea->horizontalScrollBar()->maximum(), ui->daysGridScrollArea->verticalScrollBar()->maximum());
}
