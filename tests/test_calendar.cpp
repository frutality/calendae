#include "calendar/calendar.h"

#include <QTest>

class TestCalendar : public QObject
{
    Q_OBJECT
private slots:
    void parsesCalendarList();
    void rejectsMalformedJson();
    void cborRoundTripsCalendar();
    void fromCborRejectsEntryMissingIdOrSummary();
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

void TestCalendar::cborRoundTripsCalendar()
{
    Calendar source;
    source.id = QStringLiteral("primary@example.com");
    source.summary = QStringLiteral("Primary");
    source.color = QColor(QStringLiteral("#0088aa"));
    source.selected = true;
    source.accessRole = QStringLiteral("owner");
    source.primary = true;

    const auto restored = Calendar::fromCbor(source.toCbor());
    QVERIFY(restored.has_value());
    QCOMPARE(restored->id, source.id);
    QCOMPARE(restored->summary, source.summary);
    QCOMPARE(restored->color, source.color);
    QCOMPARE(restored->selected, true);
    QCOMPARE(restored->accessRole, QStringLiteral("owner"));
    QCOMPARE(restored->primary, true);
}

void TestCalendar::fromCborRejectsEntryMissingIdOrSummary()
{
    Calendar source;
    source.id = QStringLiteral("a@example.com");
    source.summary = QStringLiteral("Work");

    QCborMap missingId = source.toCbor();
    missingId.remove(QStringLiteral("id"));
    QVERIFY(!Calendar::fromCbor(missingId).has_value());

    QCborMap missingSummary = source.toCbor();
    missingSummary.remove(QStringLiteral("summary"));
    QVERIFY(!Calendar::fromCbor(missingSummary).has_value());
}

QTEST_APPLESS_MAIN(TestCalendar)
#include "test_calendar.moc"
