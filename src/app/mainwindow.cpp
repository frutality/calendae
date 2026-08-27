#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "calendar/calendarsidebarwidget.h"
#include "calendar/event.h"
#include "calendar/eventdialog.h"
#include "calendar/eventscontroller.h"
#include "calendar/googlecalendarapi.h"
#include "calendar/montheventscontroller.h"
#include "calendar/monthviewwidget.h"
#include "calendar/timegrideventscontroller.h"
#include "calendar/timegridviewwidget.h"
#include "processmemory.h"

#include <QLabel>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_authManager(new AuthManager(this))
{
    ui->setupUi(this);

    m_memoryUsageLabel = new QLabel(this);
    m_memoryUsageLabel->setContentsMargins(0, 0, 8, 0);
    menuBar()->setCornerWidget(m_memoryUsageLabel, Qt::TopRightCorner);
    auto *memoryUsageTimer = new QTimer(this);
    connect(memoryUsageTimer, &QTimer::timeout, this, [this] {
        m_memoryUsageLabel->setText(formatMemorySize(currentProcessResidentMemoryBytes()));
    });
    memoryUsageTimer->start(2000);
    m_memoryUsageLabel->setText(formatMemorySize(currentProcessResidentMemoryBytes()));

    m_calendarSidebar = new CalendarSidebarWidget(this);
    ui->sidebarHost->layout()->addWidget(m_calendarSidebar);

    m_viewStack = new QStackedWidget(this);
    ui->monthViewHost->layout()->addWidget(m_viewStack);

    m_monthView = new MonthViewWidget(this);
    m_weekView = new TimeGridViewWidget(7, this);
    m_dayView = new TimeGridViewWidget(1, this);
    m_viewStack->addWidget(m_monthView);
    m_viewStack->addWidget(m_weekView);
    m_viewStack->addWidget(m_dayView);

    m_calendarApi = new GoogleCalendarApi(m_authManager, this);
    m_monthEventsController = new MonthEventsController(m_authManager, m_calendarApi, m_monthView, this);
    m_weekEventsController = new TimeGridEventsController(m_authManager, m_calendarApi, m_weekView, this);
    m_dayEventsController = new TimeGridEventsController(m_authManager, m_calendarApi, m_dayView, this);
    m_eventsControllers = {m_monthEventsController, m_weekEventsController, m_dayEventsController};

    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setSizes({240, 560});
    ui->mainSplitter->setChildrenCollapsible(false);
    ui->sidebarHost->setMinimumWidth(140);

    connect(ui->monthViewButton, &QPushButton::clicked, this, [this] { m_viewStack->setCurrentWidget(m_monthView); });
    connect(ui->weekViewButton, &QPushButton::clicked, this, [this] {
        ensureControllerPopulated(m_weekEventsController, m_weekControllerPopulated);
        m_viewStack->setCurrentWidget(m_weekView);
    });
    connect(ui->dayViewButton, &QPushButton::clicked, this, [this] {
        ensureControllerPopulated(m_dayEventsController, m_dayControllerPopulated);
        m_viewStack->setCurrentWidget(m_dayView);
    });

    connect(ui->signInButton, &QPushButton::clicked, this, [this] { m_authManager->signIn(this); });
    connect(ui->actionSignOut, &QAction::triggered, m_authManager, &AuthManager::signOut);

    connect(m_authManager, &AuthManager::stateChanged, this, &MainWindow::updateUiForState);
    connect(m_authManager, &AuthManager::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });

    connect(m_authManager, &AuthManager::signedIn, m_calendarApi, &GoogleCalendarApi::fetchCalendarList);
    connect(m_authManager, &AuthManager::signedOut, m_calendarSidebar, &CalendarSidebarWidget::clear);
    for (EventsController *controller : std::as_const(m_eventsControllers))
        connect(m_authManager, &AuthManager::signedOut, controller, &EventsController::clear);
    connect(m_authManager, &AuthManager::signedOut, this, [this] {
        m_calendars.clear();
        m_weekControllerPopulated = false;
        m_dayControllerPopulated = false;
    });

    connect(m_calendarApi, &GoogleCalendarApi::calendarListFetched, this, [this](const QList<Calendar> &calendars) {
        if (m_authManager->state() != AuthManager::AuthState::SignedIn)
            return; // a late reply arrived after sign-out
        m_calendars = calendars;
        m_calendarSidebar->setCalendars(calendars);
        m_monthEventsController->setCalendars(calendars); // eager: month is the default visible view
        m_weekControllerPopulated = false; // week/day populate lazily, on first switch to that view
        m_dayControllerPopulated = false;
    });
    connect(m_calendarApi, &GoogleCalendarApi::calendarListFetchFailed, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });
    connect(m_calendarSidebar, &CalendarSidebarWidget::visibilitySetRequested,
            m_calendarApi, &GoogleCalendarApi::setCalendarSelected);
    connect(m_calendarSidebar, &CalendarSidebarWidget::visibilitySetRequested, this,
            [this](const QString &calendarId, bool selected) { updateCachedCalendarSelected(calendarId, selected); });
    for (EventsController *controller : std::as_const(m_eventsControllers)) {
        connect(m_calendarSidebar, &CalendarSidebarWidget::visibilitySetRequested,
                controller, &EventsController::setCalendarEnabled);
        connect(controller, &EventsController::eventFetchFailed, this, [this](const QString &message) {
            statusBar()->showMessage(message, 8000);
        });
    }
    connect(m_calendarApi, &GoogleCalendarApi::calendarSelectedChangeFailed, this,
            [this](const QString &calendarId, bool revertToSelected, const QString &message) {
                m_calendarSidebar->setCalendarSelected(calendarId, revertToSelected);
                updateCachedCalendarSelected(calendarId, revertToSelected);
                for (EventsController *controller : std::as_const(m_eventsControllers))
                    controller->setCalendarEnabled(calendarId, revertToSelected);
                statusBar()->showMessage(message, 8000);
            });

    connect(m_monthView, &MonthViewWidget::newEventRequested, this, [this](const QDate &date) { openNewEventDialog(date); });
    connect(m_monthView, &MonthViewWidget::eventEditRequested, this, &MainWindow::openEditEventDialog);
    for (TimeGridViewWidget *view : {m_weekView, m_dayView}) {
        connect(view, &TimeGridViewWidget::newEventRequested, this, [this](const QDate &date) { openNewEventDialog(date); });
        connect(view, &TimeGridViewWidget::newTimedEventRequested, this, [this](const QDateTime &startDateTime) {
            openNewEventDialog(startDateTime.date(), startDateTime.time());
        });
        connect(view, &TimeGridViewWidget::eventEditRequested, this, &MainWindow::openEditEventDialog);
    }

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

void MainWindow::openNewEventDialog(const QDate &date, const std::optional<QTime> &initialTime)
{
    QList<Calendar> writable;
    for (const Calendar &calendar : std::as_const(m_calendars)) {
        if (calendar.accessRole == QStringLiteral("owner")
            || calendar.accessRole == QStringLiteral("writer")
            || calendar.accessRole == QStringLiteral("writerWithoutPrivateAccess")) {
            writable.append(calendar);
        }
    }

    EventDialog dialog(writable, date, initialTime, this);
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
                for (EventsController *controller : std::as_const(m_eventsControllers))
                    controller->refreshCalendar(calendarId);
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

void MainWindow::ensureControllerPopulated(EventsController *controller, bool &populated)
{
    if (populated || m_calendars.isEmpty())
        return;
    populated = true;
    controller->setCalendars(m_calendars);
}

void MainWindow::updateCachedCalendarSelected(const QString &calendarId, bool selected)
{
    for (Calendar &calendar : m_calendars) {
        if (calendar.id == calendarId) {
            calendar.selected = selected;
            return;
        }
    }
}

std::optional<Calendar> MainWindow::findCalendar(const QString &calendarId) const
{
    for (const Calendar &calendar : std::as_const(m_calendars)) {
        if (calendar.id == calendarId)
            return calendar;
    }
    return std::nullopt;
}

std::optional<Event> MainWindow::findCachedEventAcrossViews(const QString &calendarId, const QString &eventId) const
{
    // Only the view the user actually clicked in will have this cached;
    // trying all three (month/week/day) and taking the first hit is simpler
    // and just as correct as tracking which view is currently active.
    for (EventsController *controller : std::as_const(m_eventsControllers)) {
        if (const std::optional<Event> event = controller->findCachedEvent(calendarId, eventId))
            return event;
    }
    return std::nullopt;
}

void MainWindow::openEditEventDialog(const QString &calendarId, const QString &eventId)
{
    const std::optional<Event> event = findCachedEventAcrossViews(calendarId, eventId);
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
                for (EventsController *controller : std::as_const(m_eventsControllers))
                    controller->refreshCalendar(calendarId);
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
                for (EventsController *controller : std::as_const(m_eventsControllers))
                    controller->refreshCalendar(calendarId);
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
