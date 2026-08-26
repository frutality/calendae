#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "auth/authmanager.h"
#include "calendar/calendar.h"

#include <QDate>
#include <QList>
#include <QMainWindow>
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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void updateUiForState(AuthManager::AuthState state);
    void openNewEventDialog(const QDate &date);
    void openEditEventDialog(const QString &calendarId, const QString &eventId);
    std::optional<Calendar> findCalendar(const QString &calendarId) const;

    Ui::MainWindow *ui;
    AuthManager *m_authManager;
    GoogleCalendarApi *m_calendarApi;
    CalendarSidebarWidget *m_calendarSidebar;
    MonthViewWidget *m_monthView;
    MonthEventsController *m_eventsController;
    QList<Calendar> m_calendars; // most recent calendarListFetched result
    quint64 m_nextEventCreateRequestId = 1;
    quint64 m_nextEventUpdateRequestId = 1;
    quint64 m_nextEventDeleteRequestId = 1;
};
#endif // MAINWINDOW_H
