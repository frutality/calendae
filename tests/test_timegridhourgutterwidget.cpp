#include "calendar/timegriddaycolumnwidget.h"
#include "calendar/timegridhourgutterwidget.h"

#include <QPixmap>
#include <QTest>

class TestTimeGridHourGutterWidget : public QObject
{
    Q_OBJECT
private slots:
    void constructorFixesSizeToMatchDayColumn();
    void paintEventRendersWithoutCrashing();
};

void TestTimeGridHourGutterWidget::constructorFixesSizeToMatchDayColumn()
{
    TimeGridHourGutterWidget gutter;

    QCOMPARE(gutter.width(), TimeGridHourGutterWidget::kWidth);
    QCOMPARE(gutter.height(), TimeGridDayColumnWidget::kDayHeight);
}

void TestTimeGridHourGutterWidget::paintEventRendersWithoutCrashing()
{
    TimeGridHourGutterWidget gutter;

    QPixmap pixmap(gutter.size());
    gutter.render(&pixmap);

    QVERIFY(!pixmap.isNull());
}

QTEST_MAIN(TestTimeGridHourGutterWidget)
#include "test_timegridhourgutterwidget.moc"
