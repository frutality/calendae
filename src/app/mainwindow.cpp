#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "calendar/calendarsidebarwidget.h"
#include "calendar/event.h"
#include "calendar/eventdialog.h"
#include "calendar/googlecalendarapi.h"
#include "calendar/montheventscontroller.h"
#include "calendar/monthviewwidget.h"

#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_authManager(new AuthManager(this))
{
    ui->setupUi(this);

    m_calendarSidebar = new CalendarSidebarWidget(this);
    ui->sidebarHost->layout()->addWidget(m_calendarSidebar);

    m_monthView = new MonthViewWidget(this);
    ui->monthViewHost->layout()->addWidget(m_monthView);

    m_calendarApi = new GoogleCalendarApi(m_authManager, this);
    m_eventsController = new MonthEventsController(m_authManager, m_calendarApi, m_monthView, this);

    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setSizes({240, 560});
    ui->mainSplitter->setChildrenCollapsible(false);
    ui->sidebarHost->setMinimumWidth(140);

    connect(ui->signInButton, &QPushButton::clicked, this, [this] { m_authManager->signIn(this); });
    connect(ui->actionSignOut, &QAction::triggered, m_authManager, &AuthManager::signOut);

    connect(m_authManager, &AuthManager::stateChanged, this, &MainWindow::updateUiForState);
    connect(m_authManager, &AuthManager::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });

    connect(m_authManager, &AuthManager::signedIn, m_calendarApi, &GoogleCalendarApi::fetchCalendarList);
    connect(m_authManager, &AuthManager::signedOut, m_calendarSidebar, &CalendarSidebarWidget::clear);
    connect(m_authManager, &AuthManager::signedOut, m_eventsController, &MonthEventsController::clear);
    connect(m_authManager, &AuthManager::signedOut, this, [this] { m_calendars.clear(); });

    connect(m_calendarApi, &GoogleCalendarApi::calendarListFetched, this, [this](const QList<Calendar> &calendars) {
        if (m_authManager->state() != AuthManager::AuthState::SignedIn)
            return; // a late reply arrived after sign-out
        m_calendars = calendars;
        m_calendarSidebar->setCalendars(calendars);
        m_eventsController->setCalendars(calendars);
    });
    connect(m_calendarApi, &GoogleCalendarApi::calendarListFetchFailed, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });
    connect(m_calendarSidebar, &CalendarSidebarWidget::visibilitySetRequested,
            m_calendarApi, &GoogleCalendarApi::setCalendarSelected);
    connect(m_calendarSidebar, &CalendarSidebarWidget::visibilitySetRequested,
            m_eventsController, &MonthEventsController::setCalendarEnabled);
    connect(m_calendarApi, &GoogleCalendarApi::calendarSelectedChangeFailed, this,
            [this](const QString &calendarId, bool revertToSelected, const QString &message) {
                m_calendarSidebar->setCalendarSelected(calendarId, revertToSelected);
                m_eventsController->setCalendarEnabled(calendarId, revertToSelected);
                statusBar()->showMessage(message, 8000);
            });
    connect(m_eventsController, &MonthEventsController::eventFetchFailed, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });
    connect(m_monthView, &MonthViewWidget::newEventRequested, this, &MainWindow::openNewEventDialog);
    connect(m_monthView, &MonthViewWidget::eventEditRequested, this, &MainWindow::openEditEventDialog);

    updateUiForState(m_authManager->state());
    m_authManager->restoreSession();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::updateUiForState(AuthManager::AuthState state)
{
    ui->actionSignOut->setEnabled(state == AuthManager::AuthState::SignedIn);

    switch (state) {
    case AuthManager::AuthState::SignedOut:
        ui->authStatusLabel->setText(tr("Not signed in."));
        ui->signInButton->setVisible(true);
        ui->centralStack->setCurrentWidget(ui->authGatePage);
        break;
    case AuthManager::AuthState::Restoring:
        ui->authStatusLabel->setText(tr("Restoring session…"));
        ui->signInButton->setVisible(false);
        ui->centralStack->setCurrentWidget(ui->authGatePage);
        break;
    case AuthManager::AuthState::SigningIn:
        ui->authStatusLabel->setText(tr("Signing in… waiting for your browser."));
        ui->signInButton->setVisible(false);
        ui->centralStack->setCurrentWidget(ui->authGatePage);
        break;
    case AuthManager::AuthState::SignedIn:
        ui->centralStack->setCurrentWidget(ui->mainPage);
        break;
    }
}

void MainWindow::openNewEventDialog(const QDate &date)
{
    QList<Calendar> writable;
    for (const Calendar &calendar : std::as_const(m_calendars)) {
        if (calendar.accessRole == QStringLiteral("owner")
            || calendar.accessRole == QStringLiteral("writer")
            || calendar.accessRole == QStringLiteral("writerWithoutPrivateAccess")) {
            writable.append(calendar);
        }
    }

    EventDialog dialog(writable, date, this);
    quint64 pendingRequestId = 0;

    connect(&dialog, &EventDialog::createRequested, this, [this, &dialog, &pendingRequestId](const NewEventRequest &request) {
        pendingRequestId = m_nextEventCreateRequestId++;
        dialog.setSubmitInProgress(true);
        m_calendarApi->createEvent(pendingRequestId, request);
    });

    // Connected with &dialog as context: Qt auto-disconnects these if
    // dialog is destroyed, so a stale reply can never touch a dangling
    // pointer (dialog also can't actually be destroyed mid-request, since
    // setSubmitInProgress(true) disables Cancel/close — this is defense in
    // depth, not the primary safety mechanism).
    connect(m_calendarApi, &GoogleCalendarApi::eventCreated, &dialog,
            [this, &dialog, &pendingRequestId](quint64 requestId, const QString &calendarId) {
                if (requestId != pendingRequestId)
                    return;
                m_eventsController->refreshCalendar(calendarId);
                dialog.accept();
            });
    connect(m_calendarApi, &GoogleCalendarApi::eventCreateFailed, &dialog,
            [&dialog, &pendingRequestId](quint64 requestId, const QString &, const QString &message) {
                if (requestId != pendingRequestId)
                    return;
                dialog.showSubmitError(message);
            });

    if (dialog.exec() == QDialog::Accepted)
        statusBar()->showMessage(tr("Event created."), 4000);
}

std::optional<Calendar> MainWindow::findCalendar(const QString &calendarId) const
{
    for (const Calendar &calendar : std::as_const(m_calendars)) {
        if (calendar.id == calendarId)
            return calendar;
    }
    return std::nullopt;
}

void MainWindow::openEditEventDialog(const QString &calendarId, const QString &eventId)
{
    const std::optional<Event> event = m_eventsController->findCachedEvent(calendarId, eventId);
    const std::optional<Calendar> calendar = findCalendar(calendarId);
    if (!event || !calendar) {
        statusBar()->showMessage(tr("This event is no longer available."), 4000);
        return;
    }

    EventDialog dialog(*calendar, *event, this);
    quint64 pendingRequestId = 0;
    quint64 pendingDeleteRequestId = 0;

    connect(&dialog, &EventDialog::updateRequested, this,
            [this, &dialog, &pendingRequestId](const QString &eventId, const NewEventRequest &request) {
                pendingRequestId = m_nextEventUpdateRequestId++;
                dialog.setSubmitInProgress(true);
                m_calendarApi->updateEvent(pendingRequestId, eventId, request);
            });

    connect(m_calendarApi, &GoogleCalendarApi::eventUpdated, &dialog,
            [this, &dialog, &pendingRequestId](quint64 requestId, const QString &calendarId, const QString &) {
                if (requestId != pendingRequestId)
                    return;
                m_eventsController->refreshCalendar(calendarId);
                dialog.accept();
            });
    connect(m_calendarApi, &GoogleCalendarApi::eventUpdateFailed, &dialog,
            [&dialog, &pendingRequestId](quint64 requestId, const QString &, const QString &, const QString &message) {
                if (requestId != pendingRequestId)
                    return;
                dialog.showSubmitError(message);
            });

    connect(&dialog, &EventDialog::deleteRequested, this,
            [this, &dialog, &pendingDeleteRequestId, calendarId,
             displayTitle = event->summary.isEmpty() ? tr("(No title)") : event->summary,
             isRecurringInstance = !event->recurringEventId.isEmpty()](const QString &eventId) {
                const QString text = isRecurringInstance
                    ? tr("Delete \"%1\"? This will remove only this occurrence of the recurring event. This can't be undone.").arg(displayTitle)
                    : tr("Delete \"%1\"? This can't be undone.").arg(displayTitle);
                const auto choice = QMessageBox::question(&dialog, tr("Delete Event"), text,
                                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (choice != QMessageBox::Yes)
                    return;

                pendingDeleteRequestId = m_nextEventDeleteRequestId++;
                dialog.setDeleteInProgress(true);
                m_calendarApi->deleteEvent(pendingDeleteRequestId, calendarId, eventId);
            });

    connect(m_calendarApi, &GoogleCalendarApi::eventDeleted, &dialog,
            [this, &dialog, &pendingDeleteRequestId](quint64 requestId, const QString &calendarId, const QString &) {
                if (requestId != pendingDeleteRequestId)
                    return;
                m_eventsController->refreshCalendar(calendarId);
                dialog.done(EventDialog::DeletedResult);
            });
    connect(m_calendarApi, &GoogleCalendarApi::eventDeleteFailed, &dialog,
            [&dialog, &pendingDeleteRequestId](quint64 requestId, const QString &, const QString &, const QString &message) {
                if (requestId != pendingDeleteRequestId)
                    return;
                dialog.showDeleteError(message);
            });

    const int result = dialog.exec();
    if (result == QDialog::Accepted)
        statusBar()->showMessage(tr("Event updated."), 4000);
    else if (result == EventDialog::DeletedResult)
        statusBar()->showMessage(tr("Event deleted."), 4000);
}
