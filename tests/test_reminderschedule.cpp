#include "calendar/reminderschedule.h"

#include <QTest>
#include <QTimeZone>

class TestReminderSchedule : public QObject
{
    Q_OBJECT
private slots:
    void timedEventPopupFireTime();
    void allDayEventAnchorsToLocalMidnight();
    void nonPopupOverridesIgnored();
    void missedReminderDropped();
    void reminderBeyondWindowDropped();
    void multipleOverridesOnOneEvent();
    void useDefaultWithoutOverridesProducesNothing();
    void keyForIsStableAndDistinct();
};

namespace {
Event timedEvent(const QString &id, const QDateTime &startLocal, const QList<EventReminder> &overrides)
{
    Event event;
    event.id = id;
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Standup");
    event.allDay = false;
    event.startDateTime = startLocal;
    event.endDateTime = startLocal.addSecs(1800);
    event.startDate = startLocal.date();
    event.endDate = startLocal.date();
    event.remindersUseDefault = false;
    event.reminderOverrides = overrides;
    return event;
}
} // namespace

void TestReminderSchedule::timedEventPopupFireTime()
{
    const QDateTime now(QDate(2026, 8, 28), QTime(8, 0), QTimeZone::LocalTime);
    const QDateTime start(QDate(2026, 8, 28), QTime(14, 0), QTimeZone::LocalTime);
    const Event event = timedEvent(QStringLiteral("e1"), start, {{QStringLiteral("popup"), 120}});

    const QList<PlannedReminder> planned =
        ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600));

    QCOMPARE(planned.size(), 1);
    QCOMPARE(planned.first().fireAt, QDateTime(QDate(2026, 8, 28), QTime(12, 0), QTimeZone::LocalTime));
    QCOMPARE(planned.first().reminder.eventId, QStringLiteral("e1"));
    QCOMPARE(planned.first().reminder.calendarId, QStringLiteral("cal1"));
    QCOMPARE(planned.first().reminder.title, QStringLiteral("Standup"));
    QCOMPARE(planned.first().reminder.minutesBefore, 120);
    QVERIFY(!planned.first().reminder.allDay);
    QCOMPARE(planned.first().reminder.startLocal, start);
}

void TestReminderSchedule::allDayEventAnchorsToLocalMidnight()
{
    const QDateTime now(QDate(2026, 8, 27), QTime(9, 0), QTimeZone::LocalTime);

    Event event;
    event.id = QStringLiteral("allday1");
    event.calendarId = QStringLiteral("cal1");
    event.summary = QStringLiteral("Holiday");
    event.allDay = true;
    event.startDate = QDate(2026, 8, 28);
    event.endDate = QDate(2026, 8, 29);
    event.remindersUseDefault = false;
    event.reminderOverrides = {{QStringLiteral("popup"), 30}};

    const QList<PlannedReminder> planned =
        ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600));

    QCOMPARE(planned.size(), 1);
    QCOMPARE(planned.first().fireAt, QDateTime(QDate(2026, 8, 27), QTime(23, 30), QTimeZone::LocalTime));
    QVERIFY(planned.first().reminder.allDay);
}

void TestReminderSchedule::nonPopupOverridesIgnored()
{
    const QDateTime now(QDate(2026, 8, 28), QTime(8, 0), QTimeZone::LocalTime);
    const QDateTime start(QDate(2026, 8, 28), QTime(14, 0), QTimeZone::LocalTime);
    const Event event = timedEvent(QStringLiteral("e1"), start,
                                    {{QStringLiteral("email"), 60}, {QStringLiteral("sms"), 10}});

    QVERIFY(ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600)).isEmpty());
}

void TestReminderSchedule::missedReminderDropped()
{
    // now is 13:30; a popup "60 minutes before" a 14:00 start fired at 13:00.
    const QDateTime now(QDate(2026, 8, 28), QTime(13, 30), QTimeZone::LocalTime);
    const QDateTime start(QDate(2026, 8, 28), QTime(14, 0), QTimeZone::LocalTime);
    const Event event = timedEvent(QStringLiteral("e1"), start, {{QStringLiteral("popup"), 60}});

    QVERIFY(ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600)).isEmpty());
}

void TestReminderSchedule::reminderBeyondWindowDropped()
{
    const QDateTime now(QDate(2026, 8, 28), QTime(8, 0), QTimeZone::LocalTime);
    // Event 48h out with a 10-minute reminder — its fire time is past a 36h window.
    const QDateTime start(QDate(2026, 8, 30), QTime(8, 0), QTimeZone::LocalTime);
    const Event event = timedEvent(QStringLiteral("e1"), start, {{QStringLiteral("popup"), 10}});

    QVERIFY(ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600)).isEmpty());
}

void TestReminderSchedule::multipleOverridesOnOneEvent()
{
    const QDateTime now(QDate(2026, 8, 28), QTime(8, 0), QTimeZone::LocalTime);
    const QDateTime start(QDate(2026, 8, 28), QTime(14, 0), QTimeZone::LocalTime);
    // 10-min and 2h reminders are both still ahead of 08:00; a 1-day reminder
    // fired yesterday and must be dropped.
    const Event event = timedEvent(QStringLiteral("e1"), start,
                                    {{QStringLiteral("popup"), 10},
                                     {QStringLiteral("popup"), 120},
                                     {QStringLiteral("popup"), 1440}});

    const QList<PlannedReminder> planned =
        ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600));

    QCOMPARE(planned.size(), 2);
    QCOMPARE(planned.at(0).reminder.minutesBefore, 10);
    QCOMPARE(planned.at(1).reminder.minutesBefore, 120);
}

void TestReminderSchedule::useDefaultWithoutOverridesProducesNothing()
{
    const QDateTime now(QDate(2026, 8, 28), QTime(8, 0), QTimeZone::LocalTime);
    const QDateTime start(QDate(2026, 8, 28), QTime(14, 0), QTimeZone::LocalTime);
    Event event = timedEvent(QStringLiteral("e1"), start, {});
    event.remindersUseDefault = true;

    QVERIFY(ReminderSchedule::plan({event}, now, now.addSecs(36 * 3600)).isEmpty());
}

void TestReminderSchedule::keyForIsStableAndDistinct()
{
    QCOMPARE(ReminderSchedule::keyFor(QStringLiteral("c"), QStringLiteral("e"), 10),
             ReminderSchedule::keyFor(QStringLiteral("c"), QStringLiteral("e"), 10));
    QVERIFY(ReminderSchedule::keyFor(QStringLiteral("c"), QStringLiteral("e"), 10)
            != ReminderSchedule::keyFor(QStringLiteral("c"), QStringLiteral("e"), 20));
    QVERIFY(ReminderSchedule::keyFor(QStringLiteral("c"), QStringLiteral("e1"), 10)
            != ReminderSchedule::keyFor(QStringLiteral("c"), QStringLiteral("e"), 110));
}

QTEST_APPLESS_MAIN(TestReminderSchedule)
#include "test_reminderschedule.moc"
