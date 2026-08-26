#include "calendar/calendar.h"

#include <QTest>

class TestCalendar : public QObject
{
    Q_OBJECT
private slots:
    void parsesCalendarList();
    void rejectsMalformedJson();
};

void TestCalendar::parsesCalendarList()
{
    const QByteArray json = R"({
        "kind": "calendar#calendarList",
        "items": [
            {
                "id": "primary@example.com",
                "summary": "Primary",
                "backgroundColor": "#0088aa",
                "selected": true,
                "accessRole": "owner",
                "primary": true
            },
            {
                "id": "holidays@example.com",
                "summary": "Holidays",
                "accessRole": "reader"
            },
            {
                "id": "bad-color@example.com",
                "summary": "Bad Color",
                "backgroundColor": "not-a-color",
                "selected": true
            }
        ]
    })";

    QString error;
    const auto calendars = Calendar::listFromJson(json, &error);
    QVERIFY(calendars.has_value());
    QVERIFY(error.isEmpty());
    QCOMPARE(calendars->size(), 3);

    QCOMPARE((*calendars)[0].id, QStringLiteral("primary@example.com"));
    QCOMPARE((*calendars)[0].summary, QStringLiteral("Primary"));
    QCOMPARE((*calendars)[0].color, QColor(QStringLiteral("#0088aa")));
    QCOMPARE((*calendars)[0].selected, true);
    QCOMPARE((*calendars)[0].accessRole, QStringLiteral("owner"));
    QCOMPARE((*calendars)[0].primary, true);

    // "selected" omitted entirely -> Google's convention is that this means false.
    QCOMPARE((*calendars)[1].selected, false);
    QCOMPARE((*calendars)[1].accessRole, QStringLiteral("reader"));
    // "primary" omitted entirely -> Google's convention is that this means false.
    QCOMPARE((*calendars)[1].primary, false);

    // Invalid backgroundColor -> falls back to a valid color rather than an invalid QColor.
    QVERIFY((*calendars)[2].color.isValid());
}

void TestCalendar::rejectsMalformedJson()
{
    QString error;
    QVERIFY(!Calendar::listFromJson("not json", &error).has_value());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!Calendar::listFromJson(R"({"kind":"calendar#calendarList"})", &error).has_value());
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TestCalendar)
#include "test_calendar.moc"
