#ifndef TIMEGRIDHOURGUTTERWIDGET_H
#define TIMEGRIDHOURGUTTERWIDGET_H

#include <QWidget>

// Left-hand gutter of the time-grid (week/day) view's scrollable body:
// paints "0:00".."23:00" hour labels aligned with TimeGridDayColumnWidget's
// gridlines. Purely decorative, no interaction.
class TimeGridHourGutterWidget : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kWidth = 56; // px; also used as the header/all-day row gutter spacer width

    explicit TimeGridHourGutterWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // TIMEGRIDHOURGUTTERWIDGET_H
