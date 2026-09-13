#include "calendar/eventpilllabel.h"

#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

namespace {
MonthDayEventItem makeItem(const QString &eventId, const QString &calendarId)
{
    MonthDayEventItem item;
    item.eventId = eventId;
    item.calendarId = calendarId;
    item.title = QStringLiteral("Standup");
    item.color = QColor(Qt::red);
    return item;
}
} // namespace

class TestEventPillLabel : public QObject
{
    Q_OBJECT
private slots:
    void constructorConfiguresNonWrappingSizePolicy();
    void matchesEventRequiresExactNonEmptyMatch();
    void setSelectedIsIdempotentAndRepaints();
    void mousePressEmitsSingleClicked();
    void mouseDoubleClickEmitsEditRequested();
};

void TestEventPillLabel::constructorConfiguresNonWrappingSizePolicy()
{
    EventPillLabel pill(makeItem(QStringLiteral("e1"), QStringLiteral("cal1")));

    QCOMPARE(pill.sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    QCOMPARE(pill.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(pill.minimumWidth(), 0);
    QVERIFY(!pill.styleSheet().isEmpty());
}

void TestEventPillLabel::matchesEventRequiresExactNonEmptyMatch()
{
    EventPillLabel pill(makeItem(QStringLiteral("e1"), QStringLiteral("cal1")));

    QVERIFY(pill.matchesEvent(QStringLiteral("cal1"), QStringLiteral("e1")));
    QVERIFY(!pill.matchesEvent(QStringLiteral("cal1"), QString())); // empty eventId never matches
    QVERIFY(!pill.matchesEvent(QStringLiteral("other"), QStringLiteral("e1")));
    QVERIFY(!pill.matchesEvent(QStringLiteral("cal1"), QStringLiteral("other")));
}

void TestEventPillLabel::setSelectedIsIdempotentAndRepaints()
{
    EventPillLabel pill(makeItem(QStringLiteral("e1"), QStringLiteral("cal1")));
    pill.resize(80, 20);

    pill.setSelected(false); // already false: a no-op, must not crash
    QPixmap unselected(pill.size());
    pill.render(&unselected);
    QVERIFY(!unselected.isNull());

    pill.setSelected(true); // exercises the highlight-outline paint branch
    QPixmap selected(pill.size());
    pill.render(&selected);
    QVERIFY(!selected.isNull());

    pill.setSelected(true); // already true: a no-op
}

void TestEventPillLabel::mousePressEmitsSingleClicked()
{
    EventPillLabel pill(makeItem(QStringLiteral("e1"), QStringLiteral("cal1")));
    pill.resize(80, 20);
    pill.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pill));

    QSignalSpy spy(&pill, &EventPillLabel::singleClicked);
    QTest::mouseClick(&pill, Qt::LeftButton);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("cal1"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("e1"));
}

void TestEventPillLabel::mouseDoubleClickEmitsEditRequested()
{
    EventPillLabel pill(makeItem(QStringLiteral("e1"), QStringLiteral("cal1")));
    pill.resize(80, 20);
    pill.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pill));

    QSignalSpy editSpy(&pill, &EventPillLabel::editRequested);
    QTest::mouseDClick(&pill, Qt::LeftButton);

    QCOMPARE(editSpy.count(), 1);
    QCOMPARE(editSpy.first().at(0).toString(), QStringLiteral("cal1"));
    QCOMPARE(editSpy.first().at(1).toString(), QStringLiteral("e1"));
}

QTEST_MAIN(TestEventPillLabel)
#include "test_eventpilllabel.moc"
