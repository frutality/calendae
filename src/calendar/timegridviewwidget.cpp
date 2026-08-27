#include "timegridviewwidget.h"
#include "ui_timegridviewwidget.h"

#include "timegridalldaycellwidget.h"
#include "timegriddaycolumnwidget.h"
#include "timegridhourgutterwidget.h"
#include "timegridrange.h"

#include <QHBoxLayout>
#include <QLocale>
#include <QScrollArea>
#include <QSpacerItem>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

TimeGridViewWidget::TimeGridViewWidget(int dayCount, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TimeGridViewWidget)
    , m_dayCount(dayCount)
{
    ui->setupUi(this);

    // Header row: gutter spacer + one clickable day-name/day-number button
    // per column, column-aligned with the all-day row and scroll area below
    // via matching gutter width and equal stretch factors.
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addSpacerItem(new QSpacerItem(TimeGridHourGutterWidget::kWidth, 0, QSizePolicy::Fixed, QSizePolicy::Minimum));
    m_headerButtons.reserve(m_dayCount);
    for (int i = 0; i < m_dayCount; ++i) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, [this, i] { onColumnClicked(m_rangeStart.addDays(i)); });
        headerLayout->addWidget(button, 1);
        m_headerButtons.append(button);
    }
    ui->verticalLayout->addLayout(headerLayout);

    // All-day row: same gutter width, one cell per column.
    auto *allDayLayout = new QHBoxLayout;
    allDayLayout->setContentsMargins(0, 0, 0, 0);
    allDayLayout->addSpacerItem(new QSpacerItem(TimeGridHourGutterWidget::kWidth, 0, QSizePolicy::Fixed, QSizePolicy::Minimum));
    m_allDayCells.reserve(m_dayCount);
    for (int i = 0; i < m_dayCount; ++i) {
        auto *cell = new TimeGridAllDayCellWidget(this);
        connect(cell, &TimeGridAllDayCellWidget::clicked, this, &TimeGridViewWidget::onColumnClicked);
        connect(cell, &TimeGridAllDayCellWidget::doubleClicked, this, &TimeGridViewWidget::newEventRequested);
        connect(cell, &TimeGridAllDayCellWidget::eventEditRequested, this, &TimeGridViewWidget::eventEditRequested);
        allDayLayout->addWidget(cell, 1);
        m_allDayCells.append(cell);
    }
    ui->verticalLayout->addLayout(allDayLayout);

    // Scrollable half-hour grid: hour gutter + one day column per column.
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *gridHost = new QWidget;
    auto *gridLayout = new QHBoxLayout(gridHost);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);
    gridLayout->addWidget(new TimeGridHourGutterWidget(gridHost));
    m_dayColumns.reserve(m_dayCount);
    for (int i = 0; i < m_dayCount; ++i) {
        auto *column = new TimeGridDayColumnWidget(gridHost);
        connect(column, &TimeGridDayColumnWidget::clicked, this, &TimeGridViewWidget::onColumnClicked);
        connect(column, &TimeGridDayColumnWidget::slotDoubleClicked, this, &TimeGridViewWidget::newTimedEventRequested);
        connect(column, &TimeGridDayColumnWidget::eventEditRequested, this, &TimeGridViewWidget::eventEditRequested);
        gridLayout->addWidget(column, 1);
        m_dayColumns.append(column);
    }
    scrollArea->setWidget(gridHost);
    ui->verticalLayout->addWidget(scrollArea, 1);

    connect(ui->prevRangeButton, &QToolButton::clicked, this, &TimeGridViewWidget::goToPrevious);
    connect(ui->nextRangeButton, &QToolButton::clicked, this, &TimeGridViewWidget::goToNext);
    connect(ui->todayButton, &QPushButton::clicked, this, &TimeGridViewWidget::goToToday);
    connect(ui->newEventButton, &QPushButton::clicked, this, [this] { emit newEventRequested(m_selectedDate); });

    m_nowLineTimer = new QTimer(this);
    connect(m_nowLineTimer, &QTimer::timeout, this, [this] {
        for (TimeGridDayColumnWidget *column : std::as_const(m_dayColumns))
            column->refreshNowLine();
    });
    m_nowLineTimer->start(60000);

    const QDate today = QDate::currentDate();
    m_selectedDate = today;
    m_rangeStart = (m_dayCount == 7) ? TimeGridRange::weekStart(today, QLocale::system().firstDayOfWeek()) : today;

    refreshColumns();
    updateRangeLabel();
}

TimeGridViewWidget::~TimeGridViewWidget()
{
    delete ui;
}

void TimeGridViewWidget::onColumnClicked(QDate date)
{
    selectDate(date);
}

void TimeGridViewWidget::refreshColumns()
{
    const QLocale locale = QLocale::system();
    for (int i = 0; i < m_dayCount; ++i) {
        const QDate date = m_rangeStart.addDays(i);
        m_headerButtons[i]->setText(QStringLiteral("%1 %2").arg(locale.dayName(date.dayOfWeek(), QLocale::ShortFormat)).arg(date.day()));
        m_allDayCells[i]->setDate(date);
        m_dayColumns[i]->setDate(date);
    }
    updateHeaderAndColumnStates();
    rebuildAllCellEventLists();
}

void TimeGridViewWidget::updateHeaderAndColumnStates()
{
    const QDate today = QDate::currentDate();
    for (int i = 0; i < m_dayCount; ++i) {
        const QDate date = m_rangeStart.addDays(i);
        const bool isToday = date == today;
        const bool isSelected = date == m_selectedDate;

        QToolButton *button = m_headerButtons[i];
        if (isSelected) {
            button->setStyleSheet(QStringLiteral(
                "QToolButton { background-color: palette(highlight); color: palette(highlighted-text); border-radius: 4px; font-weight: bold; }"));
        } else if (isToday) {
            button->setStyleSheet(QStringLiteral("QToolButton { border: 2px solid palette(highlight); border-radius: 4px; font-weight: bold; }"));
        } else {
            button->setStyleSheet(QString());
        }

        m_dayColumns[i]->setIsToday(isToday);
    }
}

void TimeGridViewWidget::updateRangeLabel()
{
    const QLocale locale = QLocale::system();
    if (m_dayCount == 1) {
        ui->rangeLabel->setText(locale.toString(m_rangeStart, QStringLiteral("dddd, MMMM d, yyyy")));
        return;
    }

    const QDate rangeEnd = m_rangeStart.addDays(m_dayCount - 1);
    if (m_rangeStart.month() == rangeEnd.month() && m_rangeStart.year() == rangeEnd.year()) {
        ui->rangeLabel->setText(QStringLiteral("%1 %2 – %3, %4")
                                     .arg(locale.standaloneMonthName(m_rangeStart.month(), QLocale::ShortFormat))
                                     .arg(m_rangeStart.day())
                                     .arg(rangeEnd.day())
                                     .arg(m_rangeStart.year()));
    } else if (m_rangeStart.year() == rangeEnd.year()) {
        ui->rangeLabel->setText(QStringLiteral("%1 %2 – %3 %4, %5")
                                     .arg(locale.standaloneMonthName(m_rangeStart.month(), QLocale::ShortFormat))
                                     .arg(m_rangeStart.day())
                                     .arg(locale.standaloneMonthName(rangeEnd.month(), QLocale::ShortFormat))
                                     .arg(rangeEnd.day())
                                     .arg(m_rangeStart.year()));
    } else {
        ui->rangeLabel->setText(QStringLiteral("%1 %2, %3 – %4 %5, %6")
                                     .arg(locale.standaloneMonthName(m_rangeStart.month(), QLocale::ShortFormat))
                                     .arg(m_rangeStart.day())
                                     .arg(m_rangeStart.year())
                                     .arg(locale.standaloneMonthName(rangeEnd.month(), QLocale::ShortFormat))
                                     .arg(rangeEnd.day())
                                     .arg(rangeEnd.year()));
    }
}

void TimeGridViewWidget::goToPrevious()
{
    m_rangeStart = m_rangeStart.addDays(-m_dayCount);
    refreshColumns();
    updateRangeLabel();
    emit displayedRangeChanged(m_rangeStart);
}

void TimeGridViewWidget::goToNext()
{
    m_rangeStart = m_rangeStart.addDays(m_dayCount);
    refreshColumns();
    updateRangeLabel();
    emit displayedRangeChanged(m_rangeStart);
}

void TimeGridViewWidget::goToToday()
{
    const QDate today = QDate::currentDate();
    m_selectedDate = today;
    m_rangeStart = (m_dayCount == 7) ? TimeGridRange::weekStart(today, QLocale::system().firstDayOfWeek()) : today;
    refreshColumns();
    updateRangeLabel();
    emit displayedRangeChanged(m_rangeStart);
    emit dateSelected(m_selectedDate);
}

void TimeGridViewWidget::selectDate(const QDate &date)
{
    const QDate newRangeStart = (m_dayCount == 7) ? TimeGridRange::weekStart(date, QLocale::system().firstDayOfWeek()) : date;
    const bool rangeChanged = newRangeStart != m_rangeStart;
    m_selectedDate = date;

    if (rangeChanged) {
        m_rangeStart = newRangeStart;
        refreshColumns();
        updateRangeLabel();
    } else {
        updateHeaderAndColumnStates();
    }

    if (rangeChanged)
        emit displayedRangeChanged(m_rangeStart);
    emit dateSelected(m_selectedDate);
}

void TimeGridViewWidget::setEventsForCalendar(const QString &calendarId, const QHash<QDate, QList<MonthDayEventItem>> &eventsByDate)
{
    m_eventsByCalendar.insert(calendarId, eventsByDate);
    rebuildAllCellEventLists();
}

void TimeGridViewWidget::clearEventsForCalendar(const QString &calendarId)
{
    if (m_eventsByCalendar.remove(calendarId) > 0)
        rebuildAllCellEventLists();
}

void TimeGridViewWidget::clearAllEvents()
{
    m_eventsByCalendar.clear();
    rebuildAllCellEventLists();
}

void TimeGridViewWidget::rebuildAllCellEventLists()
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

    for (int i = 0; i < m_dayCount; ++i) {
        const QDate date = m_rangeStart.addDays(i);
        const QList<MonthDayEventItem> events = merged.value(date);
        m_allDayCells[i]->setEvents(events);
        m_dayColumns[i]->setEvents(events);
    }
}
