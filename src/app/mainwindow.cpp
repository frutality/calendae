#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "calendar/calendarsidebarwidget.h"
#include "calendar/event.h"
#include "calendar/eventdialog.h"
#include "calendar/eventscontroller.h"
#include "calendar/googlecalendarapi.h"
#include "calendar/montheventscontroller.h"
#include "calendar/montheventstore.h"
#include "calendar/monthviewwidget.h"
#include "calendar/reminderscheduler.h"
#include "calendar/timegrideventscontroller.h"
#include "calendar/timegridviewwidget.h"
#include "desktopnotifier.h"
#include "processmemory.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_authManager(new AuthManager(this))
{
    ui->setupUi(this);

    setupMemoryIndicator();
    createWidgets();
    setupConnectivityIndicator();
    connectViewSwitching();
    connectAuth();
    connectCalendarData();
    connectEventEditing();
    connectReminders();
    connectPeriodicRefresh();

    restoreWindowState();
    updateUiForState(m_authManager->state());
    m_authManager->restoreSession();
}

void MainWindow::setupMemoryIndicator()
{
    m_memoryUsageLabel = new QLabel(this);
    m_memoryUsageLabel->setContentsMargins(0, 0, 8, 0);
    menuBar()->setCornerWidget(m_memoryUsageLabel, Qt::TopRightCorner);
    auto *memoryUsageTimer = new QTimer(this);
    connect(memoryUsageTimer, &QTimer::timeout, this, [this] {
        m_memoryUsageLabel->setText(formatMemorySize(currentProcessResidentMemoryBytes()));
    });
    memoryUsageTimer->start(2000);
    m_memoryUsageLabel->setText(formatMemorySize(currentProcessResidentMemoryBytes()));
}

void MainWindow::setupConnectivityIndicator()
{
    m_connectivityLabel = new QLabel(this);
    m_connectivityLabel->setContentsMargins(8, 0, 8, 0);
    m_connectivityLabel->hide();
    statusBar()->addPermanentWidget(m_connectivityLabel);

    // Slow on purpose: this only exists to notice the network coming back,
    // and a successful fetch anywhere else already clears the state sooner.
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(90 * 1000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_authManager->state() == AuthManager::AuthState::SignedIn)
            m_calendarApi->fetchCalendarList(); // success -> calendarListFetched -> leaveServerUnavailable()
    });
}

void MainWindow::createWidgets()
{
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
    m_monthEventStore = new MonthEventStore(m_calendarApi, &m_eventCacheStore, this);
    m_monthEventsController = new MonthEventsController(m_authManager, m_monthEventStore, m_monthView, this);
    m_weekEventsController = new TimeGridEventsController(m_authManager, m_monthEventStore, m_weekView, this);
    m_dayEventsController = new TimeGridEventsController(m_authManager, m_monthEventStore, m_dayView, this);
    m_reminderScheduler = new ReminderScheduler(m_authManager, m_calendarApi, this);
    m_eventsControllers = {m_monthEventsController, m_weekEventsController, m_dayEventsController, m_reminderScheduler};

    m_desktopNotifier = new DesktopNotifier(this);

    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setSizes({240, 560});
    ui->mainSplitter->setChildrenCollapsible(false);
    ui->sidebarHost->setMinimumWidth(140);
}

void MainWindow::connectViewSwitching()
{
    connect(ui->monthViewButton, &QPushButton::clicked, this, [this] {
        ensureControllerPopulated(m_monthEventsController, m_monthControllerPopulated);
        m_viewStack->setCurrentWidget(m_monthView);
    });
    connect(ui->weekViewButton, &QPushButton::clicked, this, [this] {
        ensureControllerPopulated(m_weekEventsController, m_weekControllerPopulated);
        m_viewStack->setCurrentWidget(m_weekView);
    });
    connect(ui->dayViewButton, &QPushButton::clicked, this, [this] {
        ensureControllerPopulated(m_dayEventsController, m_dayControllerPopulated);
        m_viewStack->setCurrentWidget(m_dayView);
    });
}

void MainWindow::connectAuth()
{
    connect(ui->signInButton, &QPushButton::clicked, this, [this] { m_authManager->signIn(this); });
    connect(ui->actionSignOut, &QAction::triggered, m_authManager, &AuthManager::signOut);

    connect(m_authManager, &AuthManager::stateChanged, this, &MainWindow::updateUiForState);
    connect(m_authManager, &AuthManager::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });

    // Runs before the fetchCalendarList() connect below, so the disk-cached
    // calendar list and events can paint immediately while the network
    // request is still in flight (or failing, offline).
    connect(m_authManager, &AuthManager::signedIn, this, [this] {
        m_eventCacheStore.setAccountKey(m_authManager->accountKey());
        m_eventCacheStore.prune(30, 200);
        if (m_calendars.isEmpty()) {
            if (const std::optional<QList<Calendar>> cached = m_eventCacheStore.loadCalendars())
                applyCalendarList(*cached, /*fromCache=*/true);
        }
    });
    connect(m_authManager, &AuthManager::signedIn, m_calendarApi, &GoogleCalendarApi::fetchCalendarList);
    connect(m_authManager, &AuthManager::signedOut, m_calendarSidebar, &CalendarSidebarWidget::clear);
    for (EventsController *controller : std::as_const(m_eventsControllers))
        connect(m_authManager, &AuthManager::signedOut, controller, &EventsController::clear);
    connect(m_authManager, &AuthManager::signedOut, this, [this] {
        m_calendars.clear();
        m_monthEventStore->invalidateAll(/*alsoDisk=*/true); // explicit sign-out: drop this account's cache
        m_eventCacheStore.setAccountKey(QString());
        // Clear any "offline" state silently — no "reconnected" toast on sign-out.
        m_serverUnavailable = false;
        m_reconnectTimer->stop();
        m_connectivityLabel->hide();
        m_monthControllerPopulated = false;
        m_weekControllerPopulated = false;
        m_dayControllerPopulated = false;
    });
}

void MainWindow::connectCalendarData()
{
    connect(m_calendarApi, &GoogleCalendarApi::calendarListFetched, this, [this](const QList<Calendar> &calendars) {
        if (m_authManager->state() != AuthManager::AuthState::SignedIn)
            return; // a late reply arrived after sign-out
        leaveServerUnavailable(); // a successful reply proves we're back online
        m_eventCacheStore.storeCalendars(calendars);
        applyCalendarList(calendars, /*fromCache=*/false);
    });
    connect(m_calendarApi, &GoogleCalendarApi::calendarListFetchFailed, this, [this](const QString &message, bool transient) {
        if (transient)
            enterServerUnavailable();
        else
            statusBar()->showMessage(message, 8000);
    });
    // Connectivity tracking off the shared store: a transient bucket failure
    // means "offline"; a successful server refresh means "back online".
    connect(m_monthEventStore, &MonthEventStore::bucketFetchFailed, this,
            [this](const QDate &, const QString &, const QString &, bool transient) {
                if (transient)
                    enterServerUnavailable();
            });
    connect(m_monthEventStore, &MonthEventStore::bucketRefreshFailed, this,
            [this](const QDate &, const QString &, const QString &, bool transient) {
                if (transient)
                    enterServerUnavailable();
            });
    connect(m_monthEventStore, &MonthEventStore::bucketRefreshed, this,
            [this](const QDate &, const QString &) { leaveServerUnavailable(); });
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
}

void MainWindow::connectEventEditing()
{
    connect(m_monthView, &MonthViewWidget::newEventRequested, this, [this](const QDate &date) { openNewEventDialog(date); });
    connect(m_monthView, &MonthViewWidget::eventEditRequested, this, &MainWindow::openEditEventDialog);
    for (TimeGridViewWidget *view : {m_weekView, m_dayView}) {
        connect(view, &TimeGridViewWidget::newEventRequested, this, [this](const QDate &date) { openNewEventDialog(date); });
        connect(view, &TimeGridViewWidget::newTimedEventRequested, this, [this](const QDateTime &startDateTime) {
            openNewEventDialog(startDateTime.date(), startDateTime.time());
        });
        connect(view, &TimeGridViewWidget::eventEditRequested, this, &MainWindow::openEditEventDialog);
    }
}

void MainWindow::connectReminders()
{
    connect(m_reminderScheduler, &ReminderScheduler::reminderDue, m_desktopNotifier, &DesktopNotifier::notify);

    // Whenever a view pulls a fresh batch of events (month/week/day
    // navigation, first switch to a view, sidebar toggle), let the reminder
    // scheduler re-scan too — otherwise an event added in the Google web UI
    // while calendae is open isn't picked up until the 10-minute periodic
    // refresh. refreshAll() is throttled, so this stays cheap.
    for (EventsController *controller : std::as_const(m_eventsControllers)) {
        if (controller == m_reminderScheduler)
            continue;
        connect(controller, &EventsController::fetchCycleFinished,
                m_reminderScheduler, &ReminderScheduler::refreshAll);
    }

    // The Debug menu (currently just "Test Notification", which fires a
    // sample DueReminder straight through DesktopNotifier) is hidden unless
    // CALENDAE_DEBUG_MENU is set in the environment — kept around for
    // eyeballing the native notification without waiting for a real event.
    ui->menuDebug->menuAction()->setVisible(qEnvironmentVariableIsSet("CALENDAE_DEBUG_MENU"));
    connect(ui->actionTestNotification, &QAction::triggered, this, [this] {
        DueReminder sample;
        sample.calendarId = QStringLiteral("test");
        sample.eventId = QStringLiteral("test");
        sample.title = tr("Test event");
        sample.startLocal = QDateTime::currentDateTime().addSecs(600);
        sample.allDay = false;
        sample.minutesBefore = 10;
        m_desktopNotifier->notify(sample);
    });
}

void MainWindow::connectPeriodicRefresh()
{
    // A background safety net: even with no navigation, no view switch and no
    // sidebar toggle, re-pull the visible range from the server every 10
    // minutes so an event added/edited/deleted in the Google web UI while
    // calendae sits open shows up on its own. Only the on-screen view's
    // controller is polled — the other two re-sync on the next switch to
    // them. ReminderScheduler keeps its own independent 10-minute cycle.
    m_periodicRefreshTimer = new QTimer(this);
    m_periodicRefreshTimer->setInterval(10 * 60 * 1000);
    connect(m_periodicRefreshTimer, &QTimer::timeout, this, [this] {
        if (m_authManager->state() != AuthManager::AuthState::SignedIn)
            return;
        if (m_serverUnavailable)
            return; // m_reconnectTimer + calendarListFetched heal the views on recovery
        activeViewController()->refreshVisibleFromServer();
    });

    connect(m_authManager, &AuthManager::signedIn, this, [this] { m_periodicRefreshTimer->start(); });
    connect(m_authManager, &AuthManager::signedOut, this, [this] { m_periodicRefreshTimer->stop(); });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveWindowState()
{
    // Explicit organization/application args, deliberately not the
    // QCoreApplication-wide org/app name (which stays unset): CredentialsProvider's
    // oauth_client.json lookup goes through QStandardPaths::AppConfigLocation(),
    // which resolves differently once an organization name is set globally —
    // setting one broke that documented, stable config path. Passing the
    // names directly here keeps this settings file's location fixed without
    // touching that global property.
    QSettings settings(QStringLiteral("calendae"), QStringLiteral("calendae"));

    // Only touch a key when its value actually differs from what's already
    // stored. QSettings rewrites the whole file on sync whenever any key was
    // set (even to its existing value) or removed, so an unconditional
    // setValue()/remove() sweep here rewrote calendae.conf on every quit,
    // even when nothing about the window had changed. put() compares first
    // via a type-matched read (INI round-trips ints/bools as strings, so a
    // raw QVariant compare would spuriously differ) and treats a missing key
    // as a change so first-run still writes.
    const auto put = [&settings](const QString &key, const QVariant &value) {
        bool differs = !settings.contains(key);
        if (!differs) {
            const QVariant stored = settings.value(key);
            switch (value.typeId()) {
            case QMetaType::Bool:
                differs = stored.toBool() != value.toBool();
                break;
            case QMetaType::Int:
                differs = stored.toInt() != value.toInt();
                break;
            case QMetaType::QPoint:
                differs = stored.toPoint() != value.toPoint();
                break;
            default:
                differs = stored.toString() != value.toString();
                break;
            }
        }
        if (differs)
            settings.setValue(key, value);
    };

    // Explicit human-readable fields instead of the saveGeometry()/
    // restoreGeometry() QByteArray blob: that API packs screen index,
    // geometry, and window-state flags into an opaque binary format (shown
    // hex-escaped in the .ini file), which is unreadable and unfixable by
    // hand. normalGeometry() is the size/position the window would have if
    // it weren't maximized — the same "don't save the maximized size as if
    // it were the restore size" problem saveGeometry() handles internally,
    // solved explicitly here instead.
    const QRect normalGeom = normalGeometry();
    put(QStringLiteral("windowMaximized"), isMaximized());
    put(QStringLiteral("windowX"), normalGeom.x());
    put(QStringLiteral("windowY"), normalGeom.y());
    put(QStringLiteral("windowWidth"), normalGeom.width());
    put(QStringLiteral("windowHeight"), normalGeom.height());
    if (settings.contains(QStringLiteral("windowGeometry")))
        settings.remove(QStringLiteral("windowGeometry")); // stale key from the old opaque-blob format

    QString lastView = QStringLiteral("month");
    QPoint scrollPosition = m_monthView->scrollPosition();
    if (m_viewStack->currentWidget() == m_weekView) {
        lastView = QStringLiteral("week");
        scrollPosition = m_weekView->scrollPosition();
    } else if (m_viewStack->currentWidget() == m_dayView) {
        lastView = QStringLiteral("day");
        scrollPosition = m_dayView->scrollPosition();
    }
    put(QStringLiteral("lastView"), lastView);
    put(QStringLiteral("lastViewScrollPosition"), scrollPosition);
}

void MainWindow::restoreWindowState()
{
    const QSettings settings(QStringLiteral("calendae"), QStringLiteral("calendae"));

    if (settings.contains(QStringLiteral("windowWidth"))) {
        const int width = settings.value(QStringLiteral("windowWidth")).toInt();
        const int height = settings.value(QStringLiteral("windowHeight")).toInt();
        const QPoint topLeft(settings.value(QStringLiteral("windowX")).toInt(), settings.value(QStringLiteral("windowY")).toInt());

        // Only trust the saved position if it's still on a currently
        // connected screen — e.g. an external monitor that's since been
        // unplugged could otherwise place the window somewhere unreachable.
        // The size is safe to restore regardless (saveGeometry() handled
        // this screen-availability case internally; doing it explicitly
        // here is the price of the format being human-readable instead).
        bool positionOnScreen = false;
        for (const QScreen *screen : QGuiApplication::screens()) {
            if (screen->geometry().contains(topLeft)) {
                positionOnScreen = true;
                break;
            }
        }

        if (positionOnScreen)
            setGeometry(topLeft.x(), topLeft.y(), width, height);
        else
            resize(width, height);
    }
    if (settings.value(QStringLiteral("windowMaximized"), false).toBool())
        setWindowState(windowState() | Qt::WindowMaximized);

    const QString lastView = settings.value(QStringLiteral("lastView"), QStringLiteral("month")).toString();

    QWidget *targetView = m_monthView;
    QPushButton *targetButton = ui->monthViewButton;
    EventsController *targetController = m_monthEventsController;
    if (lastView == QStringLiteral("week")) {
        targetView = m_weekView;
        targetButton = ui->weekViewButton;
        targetController = m_weekEventsController;
    } else if (lastView == QStringLiteral("day")) {
        targetView = m_dayView;
        targetButton = ui->dayViewButton;
        targetController = m_dayEventsController;
    }
    m_viewStack->setCurrentWidget(targetView);
    targetButton->setChecked(true);
    // Deliberately doesn't call ensureControllerPopulated() here: sign-in
    // hasn't happened yet at construction time (m_calendars is still
    // empty), so it would no-op anyway. The calendarListFetched handler
    // populates whichever view m_viewStack->currentWidget() is once data
    // actually arrives.

    // A scroll area's scrollbar range isn't known until this widget has
    // actually been shown and laid out by the window system —
    // restoreGeometry() above works immediately because it sets the
    // window's own geometry directly, but setScrollPosition() would get
    // silently clamped to 0 if applied before that, so the first poll
    // attempt is deferred via QTimer::singleShot(0, ...) (post-show(), the
    // standard Qt idiom for this).
    const QPoint scrollPosition = settings.value(QStringLiteral("lastViewScrollPosition"), QPoint(0, 0)).toPoint();
    QTimer::singleShot(0, this, [this, targetView, scrollPosition] {
        reapplyScrollUntilSettled(targetView, scrollPosition, QPoint(-1, -1), 0, 40);
    });

    // Kicked off again once real data has actually arrived: sign-in and the
    // initial events.list fetch are both async network round-trips that
    // finish well after the polling above may have already given up
    // watching an empty/placeholder-sized grid, so this gives the polling
    // loop a fresh budget starting from when content is known to be
    // (im)minently real.
    connect(targetController, &EventsController::fetchCycleFinished, this,
            [this, targetView, scrollPosition] { reapplyScrollUntilSettled(targetView, scrollPosition, QPoint(-1, -1), 0, 40); },
            Qt::SingleShotConnection);
}

void MainWindow::reapplyScrollUntilSettled(QWidget *targetView, const QPoint &scrollPosition, const QPoint &previousMax, int stableCount,
                                            int attemptsRemaining)
{
    QPoint currentMax;
    if (auto *monthView = qobject_cast<MonthViewWidget *>(targetView)) {
        monthView->setScrollPosition(scrollPosition);
        currentMax = monthView->maxScrollPosition();
    } else if (auto *timeGridView = qobject_cast<TimeGridViewWidget *>(targetView)) {
        timeGridView->setScrollPosition(scrollPosition);
        currentMax = timeGridView->maxScrollPosition();
    }

    constexpr int kRequiredStableChecks = 3;
    const int newStableCount = (currentMax == previousMax) ? stableCount + 1 : 0;
    if (newStableCount >= kRequiredStableChecks || attemptsRemaining <= 1)
        return; // converged (or gave up as a safety net against a pathological case)

    QTimer::singleShot(50, this, [this, targetView, scrollPosition, currentMax, newStableCount, attemptsRemaining] {
        reapplyScrollUntilSettled(targetView, scrollPosition, currentMax, newStableCount, attemptsRemaining - 1);
    });
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
                // Drop the shared cache for this calendar once, then let
                // every controller re-pull the range it currently shows.
                m_monthEventStore->invalidateCalendar(calendarId);
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

EventsController *MainWindow::activeViewController() const
{
    if (m_viewStack->currentWidget() == m_weekView)
        return m_weekEventsController;
    if (m_viewStack->currentWidget() == m_dayView)
        return m_dayEventsController;
    return m_monthEventsController;
}

void MainWindow::applyCalendarList(const QList<Calendar> &calendars, bool fromCache)
{
    Q_UNUSED(fromCache); // the flag documents intent; behaviour is identical
    m_calendars = calendars;
    m_calendarSidebar->setCalendars(calendars);

    // The reminder scheduler isn't tied to any on-screen view, so it always
    // (re)populates here rather than going through the lazy
    // ensureControllerPopulated() path the three views use.
    m_reminderScheduler->setCalendars(calendars);

    // Only the view actually on screen populates eagerly (matters when
    // startup restored straight into week/day); the other two populate
    // lazily, on first switch to them.
    m_monthControllerPopulated = false;
    m_weekControllerPopulated = false;
    m_dayControllerPopulated = false;
    if (m_viewStack->currentWidget() == m_weekView)
        ensureControllerPopulated(m_weekEventsController, m_weekControllerPopulated);
    else if (m_viewStack->currentWidget() == m_dayView)
        ensureControllerPopulated(m_dayEventsController, m_dayControllerPopulated);
    else
        ensureControllerPopulated(m_monthEventsController, m_monthControllerPopulated);
}

void MainWindow::enterServerUnavailable()
{
    if (m_serverUnavailable)
        return;
    m_serverUnavailable = true;
    m_connectivityLabel->setText(tr("Google Calendar unavailable — showing saved data"));
    m_connectivityLabel->show();
    m_reconnectTimer->start();
}

void MainWindow::leaveServerUnavailable()
{
    if (!m_serverUnavailable)
        return;
    m_serverUnavailable = false;
    m_reconnectTimer->stop();
    m_connectivityLabel->hide();
    statusBar()->showMessage(tr("Reconnected to Google Calendar"), 4000);

    // Nudge the reminder schedule (which fetches independently of the store)
    // back into life. The visible views heal on their own: a leave triggered
    // by calendarListFetched re-runs applyCalendarList() right after this,
    // and buckets that failed while offline are flagged for re-fetch on the
    // next ensureMonths() anyway.
    if (m_authManager->state() == AuthManager::AuthState::SignedIn)
        m_reminderScheduler->refreshAll();
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
                m_monthEventStore->invalidateCalendar(calendarId);
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
                m_monthEventStore->invalidateCalendar(calendarId);
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
