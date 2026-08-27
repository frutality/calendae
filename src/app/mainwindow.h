#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "auth/authmanager.h"
#include "calendar/calendar.h"
#include "calendar/event.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <optional>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class CalendarSidebarWidget;
class GoogleCalendarApi;
class MonthViewWidget;
class MonthEventsController;
class TimeGridViewWidget;
class TimeGridEventsController;
class EventsController;
class QCloseEvent;
class QLabel;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // One-time construction steps, split out of the constructor purely for
    // readability. createWidgets() must run before the connect* methods —
    // it creates m_calendarApi and the events controllers they wire up.
    void setupMemoryIndicator();
    void createWidgets();
    void connectViewSwitching();
    void connectAuth();
    void connectCalendarData();
    void connectEventEditing();

    void updateUiForState(AuthManager::AuthState state);
    void openNewEventDialog(const QDate &date, const std::optional<QTime> &initialTime = std::nullopt);
    void openEditEventDialog(const QString &calendarId, const QString &eventId);
    std::optional<Calendar> findCalendar(const QString &calendarId) const;
    std::optional<Event> findCachedEventAcrossViews(const QString &calendarId, const QString &eventId) const;

    // Whichever view (month/week/day) is on screen when the calendar list
    // loads populates eagerly; the other two populate lazily, on first
    // switch to them. No-op if calendars haven't loaded yet or this
    // controller already has.
    void ensureControllerPopulated(EventsController *controller, bool &populated);

    // Keeps m_calendars' cached .selected flags in sync with sidebar
    // toggles (and their rollback on failure) — needed so that a later
    // lazy ensureControllerPopulated() call seeds week/day with the
    // calendar's *current* enabled state, not whatever it was at the last
    // calendarListFetched.
    void updateCachedCalendarSelected(const QString &calendarId, bool selected);

    // Persists/restores window geometry (position, size, maximized state —
    // as explicit, human-readable QSettings fields rather than the opaque
    // saveGeometry()/restoreGeometry() QByteArray blob) and which of
    // month/week/day was last active.
    void saveWindowState();
    void restoreWindowState();

    // Repeatedly reapplies scrollPosition to targetView (month or week/day)
    // until its maxScrollPosition() stops changing across a few consecutive
    // checks, then stops. A restored view's scrollable range keeps shifting
    // as real event content loads in (month's day cells grow taller;
    // week/day's all-day strip can too), and that can take a variable,
    // unpredictable number of internal Qt layout passes to settle — see
    // MonthDayCellWidget::resizeEvent()'s own comment about a single
    // setEventsForCalendar() call needing multiple rebuild cycles to
    // converge — so polling for actual stability beats guessing a fixed
    // number of event-loop turns to wait. previousMax/stableCount/
    // attemptsRemaining carry state across the recursive QTimer::singleShot
    // chain; callers should start with previousMax = QPoint(-1, -1), stableCount = 0.
    void reapplyScrollUntilSettled(QWidget *targetView, const QPoint &scrollPosition, const QPoint &previousMax, int stableCount,
                                    int attemptsRemaining);

    Ui::MainWindow *ui;
    AuthManager *m_authManager;
    GoogleCalendarApi *m_calendarApi;
    CalendarSidebarWidget *m_calendarSidebar;

    QStackedWidget *m_viewStack;
    MonthViewWidget *m_monthView;
    TimeGridViewWidget *m_weekView;
    TimeGridViewWidget *m_dayView;
    MonthEventsController *m_monthEventsController;
    TimeGridEventsController *m_weekEventsController;
    TimeGridEventsController *m_dayEventsController;
    QList<EventsController *> m_eventsControllers; // all three, for uniform ops
    bool m_monthControllerPopulated = false;
    bool m_weekControllerPopulated = false;
    bool m_dayControllerPopulated = false;

    QList<Calendar> m_calendars; // most recent calendarListFetched result, kept in sync with sidebar toggles
    QLabel *m_memoryUsageLabel;
    quint64 m_nextEventCreateRequestId = 1;
    quint64 m_nextEventUpdateRequestId = 1;
    quint64 m_nextEventDeleteRequestId = 1;
};
#endif // MAINWINDOW_H
