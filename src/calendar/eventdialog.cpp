#include "eventdialog.h"
#include "ui_eventdialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTimeZone>

EventDialog::EventDialog(const QList<Calendar> &writableCalendars, const QDate &initialDate,
                          const std::optional<QTime> &initialTime, QWidget *parent)
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
    ui->startTimeEdit->setTime(initialTime.value_or(defaultStartTime()));
    ui->endTimeEdit->setTime(ui->startTimeEdit->time().addSecs(3600));
    ui->allDayCheck->setChecked(false);

    setupReminderControls(-1); // new events start with no reminder

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

    int popupMinutes = -1;
    for (const EventReminder &reminder : event.reminderOverrides) {
        if (reminder.method == QStringLiteral("popup")) {
            if (popupMinutes < 0)
                popupMinutes = reminder.minutes; // this app manages a single popup reminder
        } else {
            m_preservedOverrides.append(reminder); // keep email/sms reminders intact on save
        }
    }
    setupReminderControls(popupMinutes);

    connect(ui->titleEdit, &QLineEdit::textChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->startTimeEdit, &QTimeEdit::timeChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->endTimeEdit, &QTimeEdit::timeChanged, this, &EventDialog::updateOkEnabled);
    connect(ui->allDayCheck, &QCheckBox::toggled, this, &EventDialog::onAllDayToggled);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &EventDialog::onOkClicked);

    ui->deleteButton->setVisible(true);
    connect(ui->deleteButton, &QPushButton::clicked, this, &EventDialog::onDeleteClicked);

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

    applyReminderToRequest(request);

    return request;
}

void EventDialog::setupReminderControls(int popupMinutes)
{
    struct Preset { const char *label; int minutes; };
    static const Preset kPresets[] = {
        {QT_TR_NOOP("At start of event"), 0},
        {QT_TR_NOOP("5 minutes before"), 5},
        {QT_TR_NOOP("10 minutes before"), 10},
        {QT_TR_NOOP("15 minutes before"), 15},
        {QT_TR_NOOP("30 minutes before"), 30},
        {QT_TR_NOOP("1 hour before"), 60},
        {QT_TR_NOOP("2 hours before"), 120},
        {QT_TR_NOOP("1 day before"), 1440},
    };
    for (const Preset &preset : kPresets)
        ui->reminderCombo->addItem(tr(preset.label), preset.minutes);
    ui->reminderCombo->addItem(tr("Custom…"), -1);

    ui->reminderCustomUnit->addItem(tr("minutes"), 1);
    ui->reminderCustomUnit->addItem(tr("hours"), 60);
    ui->reminderCustomUnit->addItem(tr("days"), 1440);

    m_originalReminderMinutes = popupMinutes;

    const int seedMinutes = popupMinutes < 0 ? 10 : popupMinutes;
    const int presetIndex = ui->reminderCombo->findData(seedMinutes);
    if (presetIndex >= 0) {
        ui->reminderCombo->setCurrentIndex(presetIndex);
        ui->reminderCustomValue->setValue(seedMinutes);
        ui->reminderCustomUnit->setCurrentIndex(0);
    } else {
        ui->reminderCombo->setCurrentIndex(ui->reminderCombo->count() - 1); // Custom…
        int unitIndex = 0;
        int value = seedMinutes;
        if (seedMinutes % 1440 == 0) {
            unitIndex = 2;
            value = seedMinutes / 1440;
        } else if (seedMinutes % 60 == 0) {
            unitIndex = 1;
            value = seedMinutes / 60;
        }
        ui->reminderCustomUnit->setCurrentIndex(unitIndex);
        ui->reminderCustomValue->setValue(value);
    }

    ui->reminderCheck->setChecked(popupMinutes >= 0);

    connect(ui->reminderCheck, &QCheckBox::toggled, this, &EventDialog::onReminderControlsChanged);
    connect(ui->reminderCombo, &QComboBox::currentIndexChanged, this, &EventDialog::onReminderControlsChanged);
    onReminderControlsChanged();
}

void EventDialog::onReminderControlsChanged()
{
    const bool on = ui->reminderCheck->isChecked();
    const bool custom = on && ui->reminderCombo->currentData().toInt() < 0;
    ui->reminderCombo->setEnabled(on);
    ui->reminderCustomValue->setVisible(custom);
    ui->reminderCustomUnit->setVisible(custom);
}

int EventDialog::selectedReminderMinutes() const
{
    if (!ui->reminderCheck->isChecked())
        return -1;
    const int presetMinutes = ui->reminderCombo->currentData().toInt();
    if (presetMinutes >= 0)
        return presetMinutes;
    return ui->reminderCustomValue->value() * ui->reminderCustomUnit->currentData().toInt();
}

void EventDialog::applyReminderToRequest(NewEventRequest &request) const
{
    request.preservedReminderOverrides = m_preservedOverrides;

    const int minutes = selectedReminderMinutes(); // < 0 => reminder off

    if (m_mode == Mode::Edit && minutes == m_originalReminderMinutes) {
        request.reminderMode = NewEventRequest::ReminderMode::Unchanged;
        return;
    }
    if (m_mode == Mode::Create && minutes < 0) {
        request.reminderMode = NewEventRequest::ReminderMode::Unchanged;
        return;
    }

    if (minutes >= 0) {
        request.reminderMode = NewEventRequest::ReminderMode::Popup;
        request.popupReminderMinutes = minutes;
    } else {
        request.reminderMode = NewEventRequest::ReminderMode::Off;
    }
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
    ui->reminderCheck->setEnabled(enabled);
    ui->reminderCombo->setEnabled(enabled && ui->reminderCheck->isChecked());
    ui->reminderCustomValue->setEnabled(enabled);
    ui->reminderCustomUnit->setEnabled(enabled);
    ui->buttonBox->setEnabled(enabled); // disables Ok/Cancel together
    ui->deleteButton->setEnabled(enabled);
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
