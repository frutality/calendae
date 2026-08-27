#include "timegridhourgutterwidget.h"

#include "timegriddaycolumnwidget.h"

#include <QLocale>
#include <QPainter>
#include <QTime>

TimeGridHourGutterWidget::TimeGridHourGutterWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(kWidth);
    setFixedHeight(TimeGridDayColumnWidget::kDayHeight);
}

void TimeGridHourGutterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));

    for (int hour = 1; hour < 24; ++hour) {
        const int y = hour * TimeGridDayColumnWidget::kSlotHeight * 2;
        const QString label = QLocale::system().toString(QTime(hour, 0), QLocale::ShortFormat);
        painter.drawText(QRect(0, y - 8, kWidth - 4, 16), Qt::AlignRight | Qt::AlignVCenter, label);
    }
}
