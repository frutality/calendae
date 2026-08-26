#include "eventdialog.h"
#include "ui_eventdialog.h"

#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QTimeZone>

EventDialog::EventDialog(const QList<Calendar> &writableCalendars, const QDate &initialDate, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EventDialog)
{
    ui->setupUi(this);

    int defaultIndex = -1;
    for (const Calendar &calendar : writableCalendars) {
        QPixmap swatch(12, 12);
        swatch.fill(calendar.color.isValid() ? calendar.color : QColor(Qt::gray));
        ui->calendarCombo->addItem(QIcon(swatch), calendar.summary, calendar.id);
        if (calendar.primary)
            defaultIndex = ui->calendarCombo->count() - 1;
    }
    if (defaultIndex < 0 && ui->calendarCombo->count() > 0)
        defaultIndex = 0;
    if (defaultIndex >= 0)
        ui->calendarCombo->setCurrentIndex(defaultIndex);
    if (ui->calendarCombo->count() == 0)
        ui->statusLabel->setText(tr("You don't have write access to any calendar."));

    ui->dateEdit->setDate(initialDate);
    ui->startTimeEdit->setTime(defaultStartTime());
    ui->endTimeEdit->setTime(ui->startTimeEdit->time().addSecs(3600));
    ui->allDayCheck->setChecked(false);

    connect(ui->titleEdit, &QLineEdit::textChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->startTimeEdit, &QTimeEdit::timeChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->endTimeEdit, &QTimeEdit::timeChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->allDayCheck, &QCheckBox::toggled, this, &EventDialog::onAllDayToggled);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &EventDialog::onOkClicked);

    onAllDayToggled(false);
    updateOkEnabled();
    ui->titleEdit->setFocus();
}

EventDialog::EventDialog(const Calendar &calendar, const Event &event, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EventDialog)
    , m_mode(Mode::Edit)
    , m_editingEventId(event.id)
{
    ui->setupUi(this);
    setWindowTitle(tr("Edit Event"));

    QPixmap swatch(12, 12);
    swatch.fill(calendar.color.isValid() ? calendar.color : QColor(Qt::gray));
    ui->calendarCombo->addItem(QIcon(swatch), calendar.summary, calendar.id);
    ui->calendarCombo->setCurrentIndex(0);
    ui->calendarCombo->setEnabled(false);

    ui->titleEdit->setText(event.summary);
    ui->descriptionEdit->setPlainText(event.description);
    ui->allDayCheck->setChecked(event.allDay);
    ui->dateEdit->setDate(event.startDate);
    if (!event.allDay) {
        ui->startTimeEdit->setTime(event.startDateTime.toLocalTime().time());
        ui->endTimeEdit->setTime(event.endDateTime.toLocalTime().time());
    }

    const QDate lastDateInclusive = event.allDay ? event.endDate.addDays(-1)
                                                  : event.endDateTime.toLocalTime().date();
    m_multiDayEditUnsupported = lastDateInclusive != event.startDate;

    connect(ui->titleEdit, &QLineEdit::textChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->startTimeEdit, &QTimeEdit::timeChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->endTimeEdit, &QTimeEdit::timeChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->allDayCheck, &QCheckBox::toggled, this, &EventDialog::onAllDayToggled);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &EventDialog::onOkClicked);

    QPushButton *deleteButton = ui->buttonBox->addButton(tr("Delete"), QDialogButtonBox::DestructiveRole);
    connect(deleteButton, &QPushButton::clicked, this, &EventDialog::onDeleteClicked);

    onAllDayToggled(event.allDay);

    if (m_multiDayEditUnsupported)
        ui->statusLabel->setText(tr("Editing multi-day events isn't supported yet."));
    else if (!event.recurringEventId.isEmpty())
        ui->statusLabel->setText(tr("This event is part of a recurring series. Saving affects only this occurrence."));

    updateOkEnabled();
    ui->titleEdit->setFocus();
}

EventDialog::~EventDialog()
{
    delete ui;
}

void EventDialog::updateOkEnabled()
{
    const bool valid = !ui->titleEdit->text().trimmed().isEmpty()
        && ui->calendarCombo->count() > 0
        && (ui->allDayCheck->isChecked() || ui->endTimeEdit->time() > ui->startTimeEdit->time())
        && !m_multiDayEditUnsupported;
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

void EventDialog::onAllDayToggled(bool allDay)
{
    ui->formLayout->setRowVisible(ui->startTimeEdit, !allDay);
    ui->formLayout->setRowVisible(ui->endTimeEdit, !allDay);
    updateOkEnabled();
}

void EventDialog::onOkClicked()
{
    if (m_mode == Mode::Edit)
        emit updateRequested(m_editingEventId, buildRequest());
    else
        emit createRequested(buildRequest());
}

void EventDialog::onDeleteClicked()
{
    emit deleteRequested(m_editingEventId);
}

NewEventRequest EventDialog::buildRequest() const
{
    NewEventRequest request;
    request.calendarId = ui->calendarCombo->currentData().toString();
    request.summary = ui->titleEdit->text().trimmed();
    request.description = ui->descriptionEdit->toPlainText().trimmed();
    request.allDay = ui->allDayCheck->isChecked();

    if (request.allDay) {
        request.startDate = ui->dateEdit->date();
        request.endDateExclusive = request.startDate.addDays(1);
    } else {
        const QTimeZone localTimeZone(QTimeZone::LocalTime);
        request.startDateTime = QDateTime(ui->dateEdit->date(), ui->startTimeEdit->time(), localTimeZone);
        request.endDateTime = QDateTime(ui->dateEdit->date(), ui->endTimeEdit->time(), localTimeZone);
    }

    return request;
}

void EventDialog::setFormEnabled(bool enabled)
{
    ui->calendarCombo->setEnabled(enabled);
    ui->titleEdit->setEnabled(enabled);
    ui->allDayCheck->setEnabled(enabled);
    ui->dateEdit->setEnabled(enabled);
    ui->startTimeEdit->setEnabled(enabled);
    ui->endTimeEdit->setEnabled(enabled);
    ui->descriptionEdit->setEnabled(enabled);
    ui->buttonBox->setEnabled(enabled); // disables Ok/Cancel/Delete together
}

void EventDialog::setSubmitInProgress(bool inProgress)
{
    m_submitInProgress = inProgress;
    setFormEnabled(!inProgress);
    ui->statusLabel->setText(inProgress
        ? (m_mode == Mode::Edit ? tr("Saving changes…") : tr("Creating event…"))
        : QString());
}

void EventDialog::showSubmitError(const QString &message)
{
    setSubmitInProgress(false);
    ui->statusLabel->setText(message);
}

void EventDialog::setDeleteInProgress(bool inProgress)
{
    m_deleteInProgress = inProgress;
    setFormEnabled(!inProgress);
    ui->statusLabel->setText(inProgress ? tr("Deleting…") : QString());
}

void EventDialog::showDeleteError(const QString &message)
{
    setDeleteInProgress(false);
    ui->statusLabel->setText(message);
}

void EventDialog::reject()
{
    if (m_submitInProgress || m_deleteInProgress)
        return; // ignore Escape/window-close while a request is in flight
    QDialog::reject();
}

QTime EventDialog::defaultStartTime()
{
    const QTime now = QTime::currentTime();
    const int minute = now.minute() < 30 ? 30 : 0;
    const int hour = minute == 0 ? (now.hour() + 1) % 24 : now.hour();
    return QTime(hour, minute); // acceptable cosmetic edge case near midnight: date isn't rolled, user can edit
}
