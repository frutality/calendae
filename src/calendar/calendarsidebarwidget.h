#ifndef CALENDARSIDEBARWIDGET_H
#define CALENDARSIDEBARWIDGET_H

#include "calendar.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class CalendarSidebarWidget;
}
class QListWidgetItem;
QT_END_NAMESPACE

// Purely presentational: renders a list of Calendar structs with a color
// swatch + checkable "enabled for display" state, and emits
// visibilitySetRequested when the user toggles a checkbox. Owns no
// networking; MainWindow wires it to GoogleCalendarApi.
class CalendarSidebarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CalendarSidebarWidget(QWidget *parent = nullptr);
    ~CalendarSidebarWidget() override;

public slots:
    void setCalendars(const QList<Calendar> &calendars); // full repopulate
    void clear();
    // Used for rollback after a failed PATCH. No-op if calendarId isn't present.
    void setCalendarSelected(const QString &calendarId, bool selected);

signals:
    void visibilitySetRequested(const QString &calendarId, bool selected);

private:
    void onItemChanged(QListWidgetItem *item);
    QListWidgetItem *findItem(const QString &calendarId) const;

    Ui::CalendarSidebarWidget *ui;
    bool m_updatingProgrammatically = false;
};

#endif // CALENDARSIDEBARWIDGET_H
