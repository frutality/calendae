#ifndef MONTHDAYCELLWIDGET_H
#define MONTHDAYCELLWIDGET_H

#include "montheventitem.h"

#include <QDate>
#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// One cell of the month-view grid: a day number, click handling,
// current-month/today/selected visual states, and a stack of event pills.
// Built entirely in code (no .ui).
class MonthDayCellWidget : public QWidget
{
    Q_OBJECT
public:
    // Below this width per column the month grid stops shrinking its cells
    // and the enclosing scroll area shows a horizontal scrollbar instead,
    // so event text stays readable (by scrolling) on a narrow window while
    // a wide window still divides the row into 7 equal, larger columns.
    static constexpr int kMinContentWidth = 120;

    explicit MonthDayCellWidget(QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate date() const { return m_date; }

    void setInCurrentMonth(bool inCurrentMonth);
    void setIsToday(bool isToday);
    void setSelected(bool selected);

    // Width is a fixed floor (kMinContentWidth), content-independent, so all
    // 7 grid columns stay equal; height grows with the number of stacked
    // event pills, floored at a baseline -- deliberately NOT a fixed
    // setMinimumSize(), which would make the parent grid layout ignore how
    // tall a busy day actually needs to be (Qt's qSmartMinSize() takes an
    // explicit minimumSize() verbatim, bypassing minimumSizeHint()/the
    // layout's own computed minimum entirely).
    QSize minimumSizeHint() const override;

public slots:
    void setEvents(const QList<MonthDayEventItem> &events);

    // Marks the pill for (calendarId, eventId) as selected and clears any
    // previous one; empty ids clear the selection. The chosen pill survives
    // a rebuildEventWidgets() (periodic refresh, resize) because the ids are
    // stored and re-applied there.
    void setSelectedEvent(const QString &calendarId, const QString &eventId);

signals:
    void clicked(QDate date);
    // Emitted only for a press on the cell's own empty area, not on an event
    // pill — the view uses it to clear the selected-event highlight while a
    // pill press (which also emits clicked, for date selection) keeps it.
    void backgroundClicked(QDate date);
    void doubleClicked(QDate date);
    void eventClicked(const QString &calendarId, const QString &eventId);
    void eventEditRequested(const QString &calendarId, const QString &eventId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateVisualState();
    void rebuildEventWidgets();
    void updateEventPillTexts();

    void applyEventSelection();

    QLabel *m_dayNumberLabel;
    QList<QWidget *> m_eventWidgets;
    QList<MonthDayEventItem> m_events;
    QString m_selectedCalendarId;
    QString m_selectedEventId;

    QDate m_date;
    bool m_inCurrentMonth = true;
    bool m_isToday = false;
    bool m_selected = false;
};

#endif // MONTHDAYCELLWIDGET_H
