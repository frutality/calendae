#include "calendar/calendarsidebarwidget.h"

#include <QListWidget>
#include <QSignalSpy>
#include <QTest>

namespace {
Calendar makeCalendar(const QString &id, const QString &summary, bool selected)
{
    Calendar cal;
    cal.id = id;
    cal.summary = summary;
    cal.selected = selected;
    cal.color = QColor(Qt::blue);
    return cal;
}
} // namespace

class TestCalendarSidebarWidget : public QObject
{
    Q_OBJECT
private slots:
    void setCalendarsPopulatesListWithCheckState();
    void clearEmptiesTheList();
    void setCalendarSelectedUpdatesCheckStateWithoutEmittingSignal();
    void setCalendarSelectedIgnoresUnknownCalendar();
    void userTogglingCheckboxEmitsVisibilitySetRequested();
};

void TestCalendarSidebarWidget::setCalendarsPopulatesListWithCheckState()
{
    CalendarSidebarWidget widget;
    auto *list = widget.findChild<QListWidget *>();
    QVERIFY(list);

    widget.setCalendars({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"), true),
                         makeCalendar(QStringLiteral("b"), QStringLiteral("Home"), false)});

    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("Work"));
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), QStringLiteral("a"));
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
}

void TestCalendarSidebarWidget::clearEmptiesTheList()
{
    CalendarSidebarWidget widget;
    auto *list = widget.findChild<QListWidget *>();

    widget.setCalendars({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"), true)});
    QCOMPARE(list->count(), 1);

    widget.clear();
    QCOMPARE(list->count(), 0);
}

void TestCalendarSidebarWidget::setCalendarSelectedUpdatesCheckStateWithoutEmittingSignal()
{
    CalendarSidebarWidget widget;
    auto *list = widget.findChild<QListWidget *>();
    widget.setCalendars({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"), true)});

    QSignalSpy spy(&widget, &CalendarSidebarWidget::visibilitySetRequested);
    widget.setCalendarSelected(QStringLiteral("a"), false);

    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(spy.count(), 0); // programmatic change, not a user action
}

void TestCalendarSidebarWidget::setCalendarSelectedIgnoresUnknownCalendar()
{
    CalendarSidebarWidget widget;
    widget.setCalendars({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"), true)});

    widget.setCalendarSelected(QStringLiteral("unknown"), false); // must not crash
}

void TestCalendarSidebarWidget::userTogglingCheckboxEmitsVisibilitySetRequested()
{
    CalendarSidebarWidget widget;
    auto *list = widget.findChild<QListWidget *>();
    widget.setCalendars({makeCalendar(QStringLiteral("a"), QStringLiteral("Work"), true)});

    QSignalSpy spy(&widget, &CalendarSidebarWidget::visibilitySetRequested);
    list->item(0)->setCheckState(Qt::Unchecked); // simulates the user clicking the checkbox

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("a"));
    QCOMPARE(spy.first().at(1).toBool(), false);
}

QTEST_MAIN(TestCalendarSidebarWidget)
#include "test_calendarsidebarwidget.moc"
