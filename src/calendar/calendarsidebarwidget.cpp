#include "calendarsidebarwidget.h"
#include "ui_calendarsidebarwidget.h"

#include <QListWidgetItem>
#include <QPixmap>

CalendarSidebarWidget::CalendarSidebarWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CalendarSidebarWidget)
{
    ui->setupUi(this);
    connect(ui->calendarListWidget, &QListWidget::itemChanged, this, &CalendarSidebarWidget::onItemChanged);
}

CalendarSidebarWidget::~CalendarSidebarWidget()
{
    delete ui;
}

void CalendarSidebarWidget::setCalendars(const QList<Calendar> &calendars)
{
    m_updatingProgrammatically = true;

    ui->calendarListWidget->clear();
    for (const Calendar &calendar : calendars) {
        auto *item = new QListWidgetItem(calendar.summary, ui->calendarListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(calendar.selected ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, calendar.id);

        QPixmap swatch(12, 12);
        swatch.fill(calendar.color.isValid() ? calendar.color : QColor(Qt::gray));
        item->setIcon(QIcon(swatch));
    }

    m_updatingProgrammatically = false;
}

void CalendarSidebarWidget::clear()
{
    m_updatingProgrammatically = true;
    ui->calendarListWidget->clear();
    m_updatingProgrammatically = false;
}

void CalendarSidebarWidget::setCalendarSelected(const QString &calendarId, bool selected)
{
    QListWidgetItem *item = findItem(calendarId);
    if (!item)
        return;

    m_updatingProgrammatically = true;
    item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
    m_updatingProgrammatically = false;
}

void CalendarSidebarWidget::onItemChanged(QListWidgetItem *item)
{
    if (m_updatingProgrammatically)
        return;

    const QString calendarId = item->data(Qt::UserRole).toString();
    emit visibilitySetRequested(calendarId, item->checkState() == Qt::Checked);
}

QListWidgetItem *CalendarSidebarWidget::findItem(const QString &calendarId) const
{
    for (int i = 0; i < ui->calendarListWidget->count(); ++i) {
        QListWidgetItem *item = ui->calendarListWidget->item(i);
        if (item->data(Qt::UserRole).toString() == calendarId)
            return item;
    }
    return nullptr;
}
