#include "calendar/eventdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimeEdit>
#include <QTimeZone>

namespace {

Calendar makeCalendar(const QString &id, const QString &summary, bool primary = false)
{
    Calendar cal;
    cal.id = id;
    cal.summary = summary;
    cal.primary = primary;
    return cal;
}

Event makeAllDayEvent(const QString &id, const QDate &start, const QDate &endExclusive)
{
    Event event;
    event.id = id;
    event.summary = QStringLiteral("Trip");
    event.description = QStringLiteral("desc");
    event.allDay = true;
    event.startDate = start;
    event.endDate = endExclusive;
    return event;
}

Event makeTimedEvent(const QString &id, const QDateTime &start, const QDateTime &end)
{
    Event event;
    event.id = id;
    event.summary = QStringLiteral("Standup");
    event.allDay = false;
    event.startDateTime = start;
    event.endDateTime = end;
    // Real Event::listFromJson always dual-populates startDate/endDate from
    // the local date of a timed event's instants (see event.cpp) — dateEdit
    // is seeded from startDate regardless of allDay.
    event.startDate = start.toLocalTime().date();
    event.endDate = end.toLocalTime().date();
    return event;
}

QDialogButtonBox *buttonBox(const EventDialog &dialog)
{
    return dialog.findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
}

QPushButton *okButton(const EventDialog &dialog)
{
    return buttonBox(dialog)->button(QDialogButtonBox::Ok);
}

} // namespace

class TestEventDialog : public QObject
{
    Q_OBJECT
private slots:
    void createModeDefaultsEndToOneHourAfterStart();
    void createModeUsesInitialTimeInsteadOfDefault();
    void createModeWithNoWritableCalendarsDisablesOkAndShowsMessage();
    void createModePrefersThePrimaryCalendar();

    void editModePrefillsAllDayEventInclusiveEndDate();
    void editModePrefillsTimedEventInLocalTime();
    void editModeShowsRecurringSeriesNote();
    void editModeSeedsReminderFromPopupOverridePreservingOthers();

    void okDisabledWhenTitleIsEmpty();
    void okDisabledWhenTimedOrderingIsInvalid();
    void okDisabledWhenAllDayOrderingIsInvalid();
    void allDayToggleHidesTimeRows();
    void movingStartDateShiftsEndDateByTheSameDelta();

    void buildRequestForAllDayCreateEvent();
    void buildRequestForTimedCreateEventUsesLocalTimeZone();
    void createModeOmitsReminderWhenNeverEnabled();
    void createModeSetsPopupReminderWhenEnabled();
    void editModeKeepsReminderUnchangedWhenNotModified();
    void editModeSetsReminderOffWhenUnchecked();

    void deleteButtonEmitsDeleteRequestedWithEventId();
    void setSubmitInProgressDisablesFormAndShowsStatus();
    void showSubmitErrorReenablesFormAndShowsMessage();
    void setDeleteInProgressDisablesFormAndShowsStatus();
    void rejectIsIgnoredWhileSubmitInProgress();
};

void TestEventDialog::createModeDefaultsEndToOneHourAfterStart()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15));

    auto *dateEdit = dialog.findChild<QDateEdit *>(QStringLiteral("dateEdit"));
    auto *startTimeEdit = dialog.findChild<QTimeEdit *>(QStringLiteral("startTimeEdit"));
    auto *endDateEdit = dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"));
    auto *endTimeEdit = dialog.findChild<QTimeEdit *>(QStringLiteral("endTimeEdit"));

    QCOMPARE(dateEdit->date(), QDate(2026, 8, 15));
    const QDateTime start(dateEdit->date(), startTimeEdit->time());
    const QDateTime end(endDateEdit->date(), endTimeEdit->time());
    QCOMPARE(start.secsTo(end), qint64(3600));
}

void TestEventDialog::createModeUsesInitialTimeInsteadOfDefault()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(14, 0));

    auto *startTimeEdit = dialog.findChild<QTimeEdit *>(QStringLiteral("startTimeEdit"));
    auto *endTimeEdit = dialog.findChild<QTimeEdit *>(QStringLiteral("endTimeEdit"));
    auto *endDateEdit = dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"));

    QCOMPARE(startTimeEdit->time(), QTime(14, 0));
    QCOMPARE(endTimeEdit->time(), QTime(15, 0));
    QCOMPARE(endDateEdit->date(), QDate(2026, 8, 15)); // no midnight rollover
}

void TestEventDialog::createModeWithNoWritableCalendarsDisablesOkAndShowsMessage()
{
    EventDialog dialog({}, QDate(2026, 8, 15));

    auto *calendarCombo = dialog.findChild<QComboBox *>(QStringLiteral("calendarCombo"));
    auto *statusLabel = dialog.findChild<QLabel *>(QStringLiteral("statusLabel"));

    QCOMPARE(calendarCombo->count(), 0);
    QVERIFY(!okButton(dialog)->isEnabled());
    QCOMPARE(statusLabel->text(), QStringLiteral("You don't have write access to any calendar."));
}

void TestEventDialog::createModePrefersThePrimaryCalendar()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work")),
                        makeCalendar(QStringLiteral("b"), QStringLiteral("Home"), /*primary=*/true)},
                       QDate(2026, 8, 15));

    auto *calendarCombo = dialog.findChild<QComboBox *>(QStringLiteral("calendarCombo"));
    QCOMPARE(calendarCombo->currentData().toString(), QStringLiteral("b"));
}

void TestEventDialog::editModePrefillsAllDayEventInclusiveEndDate()
{
    // A 2-day all-day event: Aug 20-21 inclusive (endDate is exclusive on the wire).
    const Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 22));
    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Edit Event"));
    auto *calendarCombo = dialog.findChild<QComboBox *>(QStringLiteral("calendarCombo"));
    QCOMPARE(calendarCombo->count(), 1);
    QVERIFY(!calendarCombo->isEnabled());
    QCOMPARE(dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->text(), QStringLiteral("Trip"));
    QCOMPARE(dialog.findChild<QPlainTextEdit *>(QStringLiteral("descriptionEdit"))->toPlainText(), QStringLiteral("desc"));
    QVERIFY(dialog.findChild<QCheckBox *>(QStringLiteral("allDayCheck"))->isChecked());
    QCOMPARE(dialog.findChild<QDateEdit *>(QStringLiteral("dateEdit"))->date(), QDate(2026, 8, 20));
    QCOMPARE(dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"))->date(), QDate(2026, 8, 21));
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->isHidden());
}

void TestEventDialog::editModePrefillsTimedEventInLocalTime()
{
    const QTimeZone localTimeZone(QTimeZone::LocalTime);
    const QDateTime start(QDate(2026, 8, 20), QTime(9, 0), localTimeZone);
    const QDateTime end(QDate(2026, 8, 20), QTime(9, 30), localTimeZone);
    const Event event = makeTimedEvent(QStringLiteral("evt1"), start, end);

    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    QCOMPARE(dialog.findChild<QDateEdit *>(QStringLiteral("dateEdit"))->date(), QDate(2026, 8, 20));
    QCOMPARE(dialog.findChild<QTimeEdit *>(QStringLiteral("startTimeEdit"))->time(), QTime(9, 0));
    QCOMPARE(dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"))->date(), QDate(2026, 8, 20));
    QCOMPARE(dialog.findChild<QTimeEdit *>(QStringLiteral("endTimeEdit"))->time(), QTime(9, 30));
}

void TestEventDialog::editModeShowsRecurringSeriesNote()
{
    Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 21));
    event.recurringEventId = QStringLiteral("series1");

    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text().contains(QStringLiteral("recurring series")));
}

void TestEventDialog::editModeSeedsReminderFromPopupOverridePreservingOthers()
{
    Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 21));
    event.reminderOverrides = {{QStringLiteral("email"), 1440}, {QStringLiteral("popup"), 30}};

    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    auto *reminderCheck = dialog.findChild<QCheckBox *>(QStringLiteral("reminderCheck"));
    auto *reminderCombo = dialog.findChild<QComboBox *>(QStringLiteral("reminderCombo"));
    QVERIFY(reminderCheck->isChecked());
    QCOMPARE(reminderCombo->currentData().toInt(), 30);
}

void TestEventDialog::okDisabledWhenTitleIsEmpty()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    QVERIFY(!okButton(dialog)->isEnabled()); // titleEdit starts empty

    auto *titleEdit = dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"));
    QTest::keyClicks(titleEdit, QStringLiteral("Standup"));
    QVERIFY(okButton(dialog)->isEnabled());
}

void TestEventDialog::okDisabledWhenTimedOrderingIsInvalid()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("Standup"));
    QVERIFY(okButton(dialog)->isEnabled());

    auto *endTimeEdit = dialog.findChild<QTimeEdit *>(QStringLiteral("endTimeEdit"));
    endTimeEdit->setTime(QTime(9, 0)); // end == start: not strictly after

    QVERIFY(!okButton(dialog)->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text(),
             QStringLiteral("The event has to end after it starts."));

    endTimeEdit->setTime(QTime(10, 0));
    QVERIFY(okButton(dialog)->isEnabled());
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text().isEmpty()); // hint cleared
}

void TestEventDialog::okDisabledWhenAllDayOrderingIsInvalid()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("Trip"));
    dialog.findChild<QCheckBox *>(QStringLiteral("allDayCheck"))->setChecked(true);

    auto *endDateEdit = dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"));
    endDateEdit->setDate(QDate(2026, 8, 14)); // before the start date

    QVERIFY(!okButton(dialog)->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text(),
             QStringLiteral("The end date can't be before the start date."));
}

void TestEventDialog::allDayToggleHidesTimeRows()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    auto *formLayout = dialog.findChild<QFormLayout *>(QStringLiteral("formLayout"));
    auto *startTimeEdit = dialog.findChild<QTimeEdit *>(QStringLiteral("startTimeEdit"));

    QVERIFY(formLayout->isRowVisible(startTimeEdit)); // not all-day initially

    dialog.findChild<QCheckBox *>(QStringLiteral("allDayCheck"))->setChecked(true);
    QVERIFY(!formLayout->isRowVisible(startTimeEdit));

    dialog.findChild<QCheckBox *>(QStringLiteral("allDayCheck"))->setChecked(false);
    QVERIFY(formLayout->isRowVisible(startTimeEdit));
}

void TestEventDialog::movingStartDateShiftsEndDateByTheSameDelta()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    auto *dateEdit = dialog.findChild<QDateEdit *>(QStringLiteral("dateEdit"));
    auto *endDateEdit = dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"));
    QCOMPARE(endDateEdit->date(), QDate(2026, 8, 15));

    dateEdit->setDate(QDate(2026, 8, 20)); // +5 days
    QCOMPARE(endDateEdit->date(), QDate(2026, 8, 20));

    dateEdit->setDate(QDate(2026, 8, 18)); // -2 days relative to the new start
    QCOMPARE(endDateEdit->date(), QDate(2026, 8, 18));
}

void TestEventDialog::buildRequestForAllDayCreateEvent()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("  Trip  "));
    dialog.findChild<QPlainTextEdit *>(QStringLiteral("descriptionEdit"))->setPlainText(QStringLiteral("  notes  "));
    dialog.findChild<QCheckBox *>(QStringLiteral("allDayCheck"))->setChecked(true);
    dialog.findChild<QDateEdit *>(QStringLiteral("endDateEdit"))->setDate(QDate(2026, 8, 16)); // last inclusive day

    QSignalSpy createdSpy(&dialog, &EventDialog::createRequested);
    okButton(dialog)->click();

    QCOMPARE(createdSpy.count(), 1);
    const auto request = createdSpy.first().first().value<NewEventRequest>();
    QCOMPARE(request.calendarId, QStringLiteral("a"));
    QCOMPARE(request.summary, QStringLiteral("Trip"));
    QCOMPARE(request.description, QStringLiteral("notes"));
    QVERIFY(request.allDay);
    QCOMPARE(request.startDate, QDate(2026, 8, 15));
    QCOMPARE(request.endDateExclusive, QDate(2026, 8, 17)); // day after the inclusive end
}

void TestEventDialog::buildRequestForTimedCreateEventUsesLocalTimeZone()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("Standup"));

    QSignalSpy createdSpy(&dialog, &EventDialog::createRequested);
    okButton(dialog)->click();

    const auto request = createdSpy.first().first().value<NewEventRequest>();
    QVERIFY(!request.allDay);
    QCOMPARE(request.startDateTime, QDateTime(QDate(2026, 8, 15), QTime(9, 0), QTimeZone(QTimeZone::LocalTime)));
    QCOMPARE(request.endDateTime, QDateTime(QDate(2026, 8, 15), QTime(10, 0), QTimeZone(QTimeZone::LocalTime)));
}

void TestEventDialog::createModeOmitsReminderWhenNeverEnabled()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("Standup"));
    QVERIFY(!dialog.findChild<QCheckBox *>(QStringLiteral("reminderCheck"))->isChecked()); // new events start with none

    QSignalSpy createdSpy(&dialog, &EventDialog::createRequested);
    okButton(dialog)->click();

    const auto request = createdSpy.first().first().value<NewEventRequest>();
    QCOMPARE(request.reminderMode, NewEventRequest::ReminderMode::Unchanged);
}

void TestEventDialog::createModeSetsPopupReminderWhenEnabled()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("Standup"));
    dialog.findChild<QCheckBox *>(QStringLiteral("reminderCheck"))->setChecked(true);
    auto *reminderCombo = dialog.findChild<QComboBox *>(QStringLiteral("reminderCombo"));
    reminderCombo->setCurrentIndex(reminderCombo->findData(15)); // "15 minutes before"

    QSignalSpy createdSpy(&dialog, &EventDialog::createRequested);
    okButton(dialog)->click();

    const auto request = createdSpy.first().first().value<NewEventRequest>();
    QCOMPARE(request.reminderMode, NewEventRequest::ReminderMode::Popup);
    QCOMPARE(request.popupReminderMinutes, 15);
}

void TestEventDialog::editModeKeepsReminderUnchangedWhenNotModified()
{
    Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 21));
    event.reminderOverrides = {{QStringLiteral("popup"), 30}};
    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    QSignalSpy updatedSpy(&dialog, &EventDialog::updateRequested);
    okButton(dialog)->click();

    QCOMPARE(updatedSpy.first().at(0).toString(), QStringLiteral("evt1"));
    const auto request = updatedSpy.first().at(1).value<NewEventRequest>();
    QCOMPARE(request.reminderMode, NewEventRequest::ReminderMode::Unchanged);
}

void TestEventDialog::editModeSetsReminderOffWhenUnchecked()
{
    Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 21));
    event.reminderOverrides = {{QStringLiteral("email"), 1440}, {QStringLiteral("popup"), 30}};
    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    dialog.findChild<QCheckBox *>(QStringLiteral("reminderCheck"))->setChecked(false);

    QSignalSpy updatedSpy(&dialog, &EventDialog::updateRequested);
    okButton(dialog)->click();

    const auto request = updatedSpy.first().at(1).value<NewEventRequest>();
    QCOMPARE(request.reminderMode, NewEventRequest::ReminderMode::Off);
    QCOMPARE(request.preservedReminderOverrides.size(), 1);
    QCOMPARE(request.preservedReminderOverrides.first().method, QStringLiteral("email"));
}

void TestEventDialog::deleteButtonEmitsDeleteRequestedWithEventId()
{
    Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 21));
    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    QSignalSpy deleteSpy(&dialog, &EventDialog::deleteRequested);
    dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->click();

    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(deleteSpy.first().first().toString(), QStringLiteral("evt1"));
}

void TestEventDialog::setSubmitInProgressDisablesFormAndShowsStatus()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));

    dialog.setSubmitInProgress(true);
    QVERIFY(!dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->isEnabled());
    QVERIFY(!buttonBox(dialog)->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text(), QStringLiteral("Creating event…"));

    dialog.setSubmitInProgress(false);
    QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->isEnabled());
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text().isEmpty());
}

void TestEventDialog::showSubmitErrorReenablesFormAndShowsMessage()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.setSubmitInProgress(true);

    dialog.showSubmitError(QStringLiteral("Network error"));

    QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text(), QStringLiteral("Network error"));
}

void TestEventDialog::setDeleteInProgressDisablesFormAndShowsStatus()
{
    Event event = makeAllDayEvent(QStringLiteral("evt1"), QDate(2026, 8, 20), QDate(2026, 8, 21));
    EventDialog dialog(makeCalendar(QStringLiteral("a"), QStringLiteral("Work")), event);

    dialog.setDeleteInProgress(true);
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text(), QStringLiteral("Deleting…"));

    dialog.showDeleteError(QStringLiteral("Could not delete"));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("statusLabel"))->text(), QStringLiteral("Could not delete"));
}

void TestEventDialog::rejectIsIgnoredWhileSubmitInProgress()
{
    EventDialog dialog({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"))}, QDate(2026, 8, 15), QTime(9, 0));
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    dialog.setSubmitInProgress(true);
    buttonBox(dialog)->button(QDialogButtonBox::Cancel)->click();
    QCOMPARE(dialog.result(), 0); // neither Accepted nor Rejected: the close was ignored

    dialog.setSubmitInProgress(false);
    buttonBox(dialog)->button(QDialogButtonBox::Cancel)->click();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
}

QTEST_MAIN(TestEventDialog)
#include "test_eventdialog.moc"
