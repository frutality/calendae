#include "auth/authmanager.h"
#include "calendar/event.h"
#include "calendar/googlecalendarapi.h"
#include "calendar/montheventstore.h"

#include <QSignalSpy>
#include <QTest>

namespace {

// Stands in for GoogleCalendarApi: records every fetchEvents() call and lets
// the test deliver replies on demand, with no network or auth involved.
class RecordingApi : public GoogleCalendarApi
{
public:
    using GoogleCalendarApi::GoogleCalendarApi;

    struct Call
    {
        QString calendarId;
        QString timeMin;
        QString timeMax;
        quint64 id;
    };
    QList<Call> calls;
    quint64 nextId = 1;

    quint64 fetchEvents(const QString &calendarId, const QString &timeMin, const QString &timeMax) override
    {
        const quint64 id = nextId++;
        calls.append({calendarId, timeMin, timeMax, id});
        return id;
    }

    void deliver(quint64 id, const QString &calendarId, const QList<Event> &events)
    {
        emit eventsFetched(id, calendarId, events);
    }
    void failRequest(quint64 id, const QString &calendarId, const QString &message)
    {
        emit eventsFetchFailed(id, calendarId, message);
    }
};

Event makeEvent(const QString &id, const QString &calendarId)
{
    Event event;
    event.id = id;
    event.calendarId = calendarId;
    event.allDay = true;
    event.startDate = QDate(2026, 8, 15);
    event.endDate = QDate(2026, 8, 16);
    return event;
}

const QDate kAug(2026, 8, 1);
const QDate kSep(2026, 9, 1);

} // namespace

class TestMonthEventStore : public QObject
{
    Q_OBJECT
private slots:
    void ensureMonthsFansOutOneRequestPerBucket();
    void repeatEnsureWhileFreshStartsNothing();
    void monthsSettledTracksInFlightBuckets();
    void eventsForReturnsLoadedBucketEvents();
    void eventsForDedupesAcrossAdjacentMonths();
    void staleReplyAfterInvalidateIsDropped();
    void failedBucketIsReFetchedOnNextEnsure();
    void invalidateAllClearsEverything();
    void sameMonthDifferentYearsAreDistinctBuckets();
};

void TestMonthEventStore::ensureMonthsFansOutOneRequestPerBucket()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);

    store.ensureMonths({QDate(2026, 8, 15)}, {QStringLiteral("a"), QStringLiteral("b")});

    QCOMPARE(api.calls.size(), 2);
    QCOMPARE(QSet<QString>({api.calls[0].calendarId, api.calls[1].calendarId}),
             QSet<QString>({QStringLiteral("a"), QStringLiteral("b")}));
    // Both buckets are the same month, so the same grid [timeMin, timeMax).
    QCOMPARE(api.calls[0].timeMin, api.calls[1].timeMin);
    QCOMPARE(api.calls[0].timeMax, api.calls[1].timeMax);
}

void TestMonthEventStore::repeatEnsureWhileFreshStartsNothing()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    store.ensureMonths({kAug}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 1);
    api.deliver(api.calls[0].id, QStringLiteral("a"), {});
    QVERIFY(updated.wait());

    store.ensureMonths({kAug}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 1); // still fresh — no new request
    QVERIFY(store.monthsSettled({kAug}, {QStringLiteral("a")}));
}

void TestMonthEventStore::monthsSettledTracksInFlightBuckets()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    store.ensureMonths({kAug}, {QStringLiteral("a"), QStringLiteral("b")});
    QVERIFY(!store.monthsSettled({kAug}, {QStringLiteral("a"), QStringLiteral("b")}));

    const auto idFor = [&](const QString &cal) {
        for (const auto &c : api.calls)
            if (c.calendarId == cal)
                return c.id;
        return quint64(0);
    };
    api.deliver(idFor(QStringLiteral("a")), QStringLiteral("a"), {});
    QVERIFY(updated.wait());
    QVERIFY(!store.monthsSettled({kAug}, {QStringLiteral("a"), QStringLiteral("b")})); // b still in flight

    api.deliver(idFor(QStringLiteral("b")), QStringLiteral("b"), {});
    QVERIFY(updated.wait());
    QVERIFY(store.monthsSettled({kAug}, {QStringLiteral("a"), QStringLiteral("b")}));
}

void TestMonthEventStore::eventsForReturnsLoadedBucketEvents()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    store.ensureMonths({kAug}, {QStringLiteral("a"), QStringLiteral("b")});
    const auto idFor = [&](const QString &cal) {
        for (const auto &c : api.calls)
            if (c.calendarId == cal)
                return c.id;
        return quint64(0);
    };
    api.deliver(idFor(QStringLiteral("a")), QStringLiteral("a"),
                {makeEvent(QStringLiteral("e1"), QStringLiteral("a")), makeEvent(QStringLiteral("e2"), QStringLiteral("a"))});
    QVERIFY(updated.wait());

    QCOMPARE(store.eventsFor(QStringLiteral("a"), {kAug}).size(), 2);
    QVERIFY(store.eventsFor(QStringLiteral("b"), {kAug}).isEmpty()); // still in flight
}

void TestMonthEventStore::eventsForDedupesAcrossAdjacentMonths()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    store.ensureMonths({kAug, kSep}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 2);

    // A month-boundary event lands in both adjacent grid buckets.
    api.deliver(api.calls[0].id, QStringLiteral("a"),
                {makeEvent(QStringLiteral("shared"), QStringLiteral("a")), makeEvent(QStringLiteral("aug-only"), QStringLiteral("a"))});
    QVERIFY(updated.wait());
    api.deliver(api.calls[1].id, QStringLiteral("a"),
                {makeEvent(QStringLiteral("shared"), QStringLiteral("a")), makeEvent(QStringLiteral("sep-only"), QStringLiteral("a"))});
    QVERIFY(updated.wait());

    QList<QString> ids;
    for (const Event &e : store.eventsFor(QStringLiteral("a"), {kAug, kSep}))
        ids.append(e.id);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, (QList<QString>{QStringLiteral("aug-only"), QStringLiteral("sep-only"), QStringLiteral("shared")}));
}

void TestMonthEventStore::staleReplyAfterInvalidateIsDropped()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    store.ensureMonths({kAug}, {QStringLiteral("a")});
    const quint64 inFlightId = api.calls[0].id;

    store.invalidateCalendar(QStringLiteral("a"));
    api.deliver(inFlightId, QStringLiteral("a"), {makeEvent(QStringLiteral("late"), QStringLiteral("a"))});

    QVERIFY(!updated.wait(100)); // reply for a dropped bucket is ignored
    QVERIFY(store.eventsFor(QStringLiteral("a"), {kAug}).isEmpty());
    QVERIFY(!store.monthsSettled({kAug}, {QStringLiteral("a")}));
}

void TestMonthEventStore::failedBucketIsReFetchedOnNextEnsure()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy failed(&store, &MonthEventStore::bucketFetchFailed);

    store.ensureMonths({kAug}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 1);
    api.failRequest(api.calls[0].id, QStringLiteral("a"), QStringLiteral("boom"));
    QVERIFY(failed.wait());

    // A failed bucket has settled (won't hang monthsSettled) but is not fresh.
    QVERIFY(store.monthsSettled({kAug}, {QStringLiteral("a")}));
    store.ensureMonths({kAug}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 2); // retried
}

void TestMonthEventStore::invalidateAllClearsEverything()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    store.ensureMonths({kAug}, {QStringLiteral("a")});
    api.deliver(api.calls[0].id, QStringLiteral("a"), {makeEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    QVERIFY(updated.wait());

    store.invalidateAll();
    QVERIFY(store.eventsFor(QStringLiteral("a"), {kAug}).isEmpty());
    QVERIFY(!store.monthsSettled({kAug}, {QStringLiteral("a")}));

    store.ensureMonths({kAug}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 2);
}

void TestMonthEventStore::sameMonthDifferentYearsAreDistinctBuckets()
{
    AuthManager auth;
    RecordingApi api(&auth);
    MonthEventStore store(&api);
    QSignalSpy updated(&store, &MonthEventStore::bucketUpdated);

    const QDate aug2025(2025, 8, 1);
    const QDate aug2026(2026, 8, 1);

    store.ensureMonths({aug2025, aug2026}, {QStringLiteral("a")});
    QCOMPARE(api.calls.size(), 2); // one bucket per (year-)month, not one shared "August"
    QVERIFY(api.calls[0].timeMin != api.calls[1].timeMin);

    const auto idForMin = [&](const QString &needle) {
        for (const auto &c : api.calls)
            if (c.timeMin.startsWith(needle))
                return c.id;
        return quint64(0);
    };
    api.deliver(idForMin(QStringLiteral("2025")), QStringLiteral("a"), {makeEvent(QStringLiteral("e-2025"), QStringLiteral("a"))});
    QVERIFY(updated.wait());
    api.deliver(idForMin(QStringLiteral("2026")), QStringLiteral("a"), {makeEvent(QStringLiteral("e-2026"), QStringLiteral("a"))});
    QVERIFY(updated.wait());

    QCOMPARE(store.eventsFor(QStringLiteral("a"), {aug2025}).size(), 1);
    QCOMPARE(store.eventsFor(QStringLiteral("a"), {aug2025}).first().id, QStringLiteral("e-2025"));
    QCOMPARE(store.eventsFor(QStringLiteral("a"), {aug2026}).size(), 1);
    QCOMPARE(store.eventsFor(QStringLiteral("a"), {aug2026}).first().id, QStringLiteral("e-2026"));
}

QTEST_GUILESS_MAIN(TestMonthEventStore)
#include "test_montheventstore.moc"
