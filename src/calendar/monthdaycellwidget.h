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

signals:
    void clicked(QDate date);
    void doubleClicked(QDate date);
    void eventEditRequested(const QString &calendarId, const QString &eventId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateVisualState();
    void rebuildEventWidgets();
    void updateEventPillTexts();

    QLabel *m_dayNumberLabel;
    QList<QWidget *> m_eventWidgets;
    QList<MonthDayEventItem> m_events;

    QDate m_date;
    bool m_inCurrentMonth = true;
    bool m_isToday = false;
    bool m_selected = false;
};

#endif // MONTHDAYCELLWIDGET_H
