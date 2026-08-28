#ifndef EVENTDIALOG_H
#define EVENTDIALOG_H

#include "calendar.h"
#include "event.h"
#include "neweventrequest.h"

#include <QDate>
#include <QDialog>
#include <QList>
#include <QTime>
#include <optional>

QT_BEGIN_NAMESPACE
namespace Ui {
class EventDialog;
}
QT_END_NAMESPACE

// A short-lived modal form for creating a new event. Owns no networking;
// MainWindow drives the actual create request and only calls accept() once
// GoogleCalendarApi confirms success (see setSubmitInProgress/
// showSubmitError), so the dialog stays open across the request and the
// user sees a clear success/failure outcome instead of a fire-and-forget
// submit.
class EventDialog : public QDialog
{
    Q_OBJECT
public:
    // writableCalendars is expected non-empty (the caller pre-filters to
    // owner/writer/writerWithoutPrivateAccess); an empty list is handled
    // defensively (OK stays disabled, explanatory text shown) rather than
    // asserted against. initialTime, when supplied (e.g. a week/day view
    // half-hour slot double-click), pre-fills a non-all-day start time
    // instead of defaultStartTime(), with the same "+1 hour" default
    // duration; the end time is still user-editable either way.
    explicit EventDialog(const QList<Calendar> &writableCalendars, const QDate &initialDate,
                          const std::optional<QTime> &initialTime = std::nullopt, QWidget *parent = nullptr);

    // Edit mode: pre-fills every field from event. calendar is shown as a
    // fixed, disabled single-item field — Google models moving an event
    // between calendars as a separate events.move call, out of scope here.
    // If the event spans more than one calendar day (rare; this dialog's
    // date model, like create's, is single-day-only), the form still opens
    // with fields populated but Save stays disabled with an explanatory
    // message, to avoid silently truncating the event's span on save.
    explicit EventDialog(const Calendar &calendar, const Event &event, QWidget *parent = nullptr);
    ~EventDialog() override;

    // exec()'s return value when the event was deleted (distinct from
    // QDialog::Accepted, used for a saved edit/create, and QDialog::Rejected,
    // used for cancel/close).
    static constexpr int DeletedResult = QDialog::Accepted + 1;

public slots:
    // Disables the whole form, including Cancel, while a create request is
    // in flight, so the dialog cannot be closed mid-request.
    void setSubmitInProgress(bool inProgress);
    // Re-enables the form (equivalent to setSubmitInProgress(false)) and
    // shows message in the inline status label.
    void showSubmitError(const QString &message);
    // Disables the whole form while a delete request is in flight. Edit
    // mode only.
    void setDeleteInProgress(bool inProgress);
    // Re-enables the form (equivalent to setDeleteInProgress(false)) and
    // shows message in the inline status label.
    void showDeleteError(const QString &message);

signals:
    // Emitted once, from a validated form, when the user clicks OK. Does
    // NOT close the dialog; the caller calls accept() itself after the
    // create request succeeds.
    void createRequested(const NewEventRequest &request);
    void updateRequested(const QString &eventId, const NewEventRequest &request);
    // Emitted immediately on clicking Delete, with no confirmation — the
    // caller (MainWindow) owns confirming and driving the actual delete
    // request, matching how this dialog never calls the API itself. Edit
    // mode only.
    void deleteRequested(const QString &eventId);

protected:
    void reject() override; // ignored while a submit is in flight

private slots:
    void updateOkEnabled();
    void onAllDayToggled(bool allDay);
    void onOkClicked();
    void onDeleteClicked();
    void onReminderControlsChanged();

private:
    enum class Mode { Create, Edit };

    NewEventRequest buildRequest() const;
    void setFormEnabled(bool enabled);
    static QTime defaultStartTime();

    // Populates the reminder combo/units and seeds them from popupMinutes
    // (< 0 means "no popup reminder"). Records the seeded state so
    // buildRequest() can tell whether the user changed it.
    void setupReminderControls(int popupMinutes);
    int selectedReminderMinutes() const; // < 0 when the "Remind me" box is unchecked
    void applyReminderToRequest(NewEventRequest &request) const;

    Ui::EventDialog *ui;
    bool m_submitInProgress = false;
    bool m_deleteInProgress = false;
    Mode m_mode = Mode::Create;
    QString m_editingEventId;
    bool m_multiDayEditUnsupported = false;

    // Reminder state as loaded (Edit mode); compared against the form in
    // buildRequest() to decide between Unchanged / Off / Popup.
    int m_originalReminderMinutes = -1; // < 0: event had no popup reminder
    QList<EventReminder> m_preservedOverrides; // non-popup overrides to carry through on save
};

#endif // EVENTDIALOG_H
