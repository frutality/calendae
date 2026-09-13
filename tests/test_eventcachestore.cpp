#include "calendar/calendar.h"
#include "calendar/event.h"
#include "calendar/eventcachestore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

Event allDayEvent(const QString &id, const QString &calendarId)
{
    Event e;
    e.id = id;
    e.calendarId = calendarId;
    e.summary = QStringLiteral("Summary ") + id;
    e.allDay = true;
    e.startDate = QDate(2026, 8, 15);
    e.endDate = QDate(2026, 8, 16);
    return e;
}

Event timedEvent(const QString &id, const QString &calendarId)
{
    Event e;
    e.id = id;
    e.calendarId = calendarId;
    e.allDay = false;
    e.startDateTime = QDateTime::fromString(QStringLiteral("2026-08-15T09:00:00Z"), Qt::ISODate);
    e.endDateTime = QDateTime::fromString(QStringLiteral("2026-08-15T10:00:00Z"), Qt::ISODate);
    e.startDate = QDate(2026, 8, 15);
    e.endDate = QDate(2026, 8, 15);
    EventReminder r;
    r.method = QStringLiteral("popup");
    r.minutes = 30;
    e.remindersUseDefault = false;
    e.reminderOverrides = {r};
    return e;
}

const QDate kAug(2026, 8, 1);
const QDate kSep(2026, 9, 1);

QStringList bucketFiles(const QString &accountDir)
{
    return QDir(accountDir).entryList({QStringLiteral("*__*.cbor")}, QDir::Files, QDir::Name);
}

void backdate(const QString &path, const QDateTime &when)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
    f.close();
}

} // namespace

class TestEventCacheStore : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsBucket();
    void isANoOpWithoutAccountKey();
    void ignoresCorruptFile();
    void skipsWriteWhenBucketUnchanged();
    void removeCalendarDropsOnlyThatCalendar();
    void removeAllWipesAccountDir();
    void pruneDropsOldAndExcessBuckets();
    void roundTripsCalendarList();
    void accountKeyNamespacesTheCache();

    void disabledStoreIsANoOpForEveryOperation();
    void bucketWithWrongSchemaVersionIsIgnored();
    void loadCalendarsBeforeAnyStoreReturnsNullopt();
    void loadCalendarsIgnoresCorruptFile();
    void loadCalendarsWithWrongSchemaVersionIsIgnored();
    void skipsWriteWhenCalendarListUnchanged();
    void removeCalendarBeforeAnyStoreIsANoOp();
    void pruneBeforeAnyStoreIsANoOp();
    void storeBucketFailsGracefullyWhenAccountDirIsBlockedByAFile();
    void storeBucketFailsGracefullyWhenCommitCannotReplaceADirectory();
};

void TestEventCacheStore::roundTripsBucket()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    const QList<Event> events{allDayEvent(QStringLiteral("e1"), QStringLiteral("cal")),
                              timedEvent(QStringLiteral("e2"), QStringLiteral("cal"))};
    store.storeBucket(kAug, QStringLiteral("cal"), events);

    const auto loaded = store.loadBucket(kAug, QStringLiteral("cal"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->events.size(), 2);
    QCOMPARE(loaded->events.at(0).id, QStringLiteral("e1"));
    QVERIFY(loaded->events.at(0).allDay);
    QCOMPARE(loaded->events.at(0).startDate, QDate(2026, 8, 15));
    QCOMPARE(loaded->events.at(1).id, QStringLiteral("e2"));
    QVERIFY(!loaded->events.at(1).allDay);
    QCOMPARE(loaded->events.at(1).startDateTime,
             QDateTime::fromString(QStringLiteral("2026-08-15T09:00:00Z"), Qt::ISODate));
    QCOMPARE(loaded->events.at(1).reminderOverrides.size(), 1);
    QCOMPARE(loaded->events.at(1).reminderOverrides.at(0).method, QStringLiteral("popup"));
    QCOMPARE(loaded->events.at(1).reminderOverrides.at(0).minutes, 30);
    QVERIFY(loaded->fetchedAt.isValid());
}

void TestEventCacheStore::isANoOpWithoutAccountKey()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    QVERIFY(!store.enabled());

    store.storeBucket(kAug, QStringLiteral("cal"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("cal"))});
    QVERIFY(!store.loadBucket(kAug, QStringLiteral("cal")).has_value());
    QVERIFY(QDir(tmp.path()).isEmpty()); // nothing was written at all
}

void TestEventCacheStore::ignoresCorruptFile()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    store.storeBucket(kAug, QStringLiteral("cal"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("cal"))});

    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    const QStringList files = bucketFiles(accountDir);
    QCOMPARE(files.size(), 1);
    QFile f(accountDir + QLatin1Char('/') + files.first());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("not cbor at all");
    f.close();

    QVERIFY(!store.loadBucket(kAug, QStringLiteral("cal")).has_value());
}

void TestEventCacheStore::skipsWriteWhenBucketUnchanged()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    const QList<Event> events{allDayEvent(QStringLiteral("e1"), QStringLiteral("cal"))};
    store.storeBucket(kAug, QStringLiteral("cal"), events);

    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    const QString path = accountDir + QLatin1Char('/') + bucketFiles(accountDir).first();
    const QDateTime marker = QDateTime::currentDateTimeUtc().addSecs(-3600);
    backdate(path, marker);

    store.storeBucket(kAug, QStringLiteral("cal"), events); // identical -> must not rewrite
    QCOMPARE(QFileInfo(path).lastModified().toUTC().toSecsSinceEpoch(), marker.toSecsSinceEpoch());

    store.storeBucket(kAug, QStringLiteral("cal"),
                      {allDayEvent(QStringLiteral("e1"), QStringLiteral("cal")),
                       allDayEvent(QStringLiteral("e2"), QStringLiteral("cal"))}); // changed -> rewrites
    QVERIFY(QFileInfo(path).lastModified().toUTC().toSecsSinceEpoch() > marker.toSecsSinceEpoch());
}

void TestEventCacheStore::removeCalendarDropsOnlyThatCalendar()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    store.storeBucket(kAug, QStringLiteral("a"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    store.storeBucket(kSep, QStringLiteral("a"), {allDayEvent(QStringLiteral("e2"), QStringLiteral("a"))});
    store.storeBucket(kAug, QStringLiteral("b"), {allDayEvent(QStringLiteral("e3"), QStringLiteral("b"))});

    store.removeCalendar(QStringLiteral("a"));

    QVERIFY(!store.loadBucket(kAug, QStringLiteral("a")).has_value());
    QVERIFY(!store.loadBucket(kSep, QStringLiteral("a")).has_value());
    QVERIFY(store.loadBucket(kAug, QStringLiteral("b")).has_value());
}

void TestEventCacheStore::removeAllWipesAccountDir()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    store.storeBucket(kAug, QStringLiteral("a"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("a"))});
    store.storeCalendars({});

    store.removeAll();

    QVERIFY(!QDir(tmp.path() + QStringLiteral("/acct")).exists());
    QVERIFY(!store.loadBucket(kAug, QStringLiteral("a")).has_value());
}

void TestEventCacheStore::pruneDropsOldAndExcessBuckets()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    const QList<QDate> months{QDate(2026, 1, 1), QDate(2026, 2, 1), QDate(2026, 3, 1),
                              QDate(2026, 4, 1), QDate(2026, 5, 1)};
    for (const QDate &m : months)
        store.storeBucket(m, QStringLiteral("a"), {allDayEvent(QStringLiteral("e"), QStringLiteral("a"))});

    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    QStringList files = bucketFiles(accountDir); // sorted by name == by month here
    QCOMPARE(files.size(), 5);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    // Oldest two are ancient, the rest are recent-but-staggered.
    backdate(accountDir + QLatin1Char('/') + files.at(0), now.addDays(-90));
    backdate(accountDir + QLatin1Char('/') + files.at(1), now.addDays(-40));
    backdate(accountDir + QLatin1Char('/') + files.at(2), now.addDays(-3));
    backdate(accountDir + QLatin1Char('/') + files.at(3), now.addDays(-2));
    backdate(accountDir + QLatin1Char('/') + files.at(4), now.addDays(-1));

    store.prune(/*maxAgeDays=*/30, /*maxBuckets=*/2);

    files = bucketFiles(accountDir);
    QCOMPARE(files.size(), 2); // two ancient dropped by age, then trimmed to the 2 newest
    QVERIFY(store.loadBucket(QDate(2026, 4, 1), QStringLiteral("a")).has_value());
    QVERIFY(store.loadBucket(QDate(2026, 5, 1), QStringLiteral("a")).has_value());
}

void TestEventCacheStore::roundTripsCalendarList()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    Calendar a;
    a.id = QStringLiteral("a@example.com");
    a.summary = QStringLiteral("Work");
    a.color = QColor(QStringLiteral("#0088aa"));
    a.selected = true;
    a.accessRole = QStringLiteral("owner");
    a.primary = true;
    Calendar b;
    b.id = QStringLiteral("b@example.com");
    b.summary = QStringLiteral("Holidays");
    b.color = QColor(Qt::gray);

    store.storeCalendars({a, b});

    const auto loaded = store.loadCalendars();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), 2);
    QCOMPARE(loaded->at(0).id, QStringLiteral("a@example.com"));
    QCOMPARE(loaded->at(0).summary, QStringLiteral("Work"));
    QCOMPARE(loaded->at(0).color, QColor(QStringLiteral("#0088aa")));
    QVERIFY(loaded->at(0).selected);
    QCOMPARE(loaded->at(0).accessRole, QStringLiteral("owner"));
    QVERIFY(loaded->at(0).primary);
    QCOMPARE(loaded->at(1).id, QStringLiteral("b@example.com"));
    QVERIFY(!loaded->at(1).selected);
}

void TestEventCacheStore::disabledStoreIsANoOpForEveryOperation()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    QVERIFY(!store.enabled());

    // Every operation must silently no-op (no crash, nothing written) while
    // no account key is set — mirrors storeBucket/loadBucket already
    // covered by isANoOpWithoutAccountKey().
    QVERIFY(!store.loadCalendars().has_value());
    store.storeCalendars({Calendar()});
    store.removeCalendar(QStringLiteral("a"));
    store.removeAll();
    store.prune(30, 5);

    QVERIFY(QDir(tmp.path()).isEmpty());
}

void TestEventCacheStore::bucketWithWrongSchemaVersionIsIgnored()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    // Reproduce the on-disk layout documented in eventcachestore.h directly,
    // with an incompatible schema version.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayLiteral("cal"), QCryptographicHash::Sha1).toHex());
    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    QVERIFY(QDir().mkpath(accountDir));
    const QString path = accountDir + QStringLiteral("/2026-08__%1.cbor").arg(hash);

    const QCborMap map{{QLatin1String("v"), 999}, {QLatin1String("events"), QCborArray()}};
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QCborValue(map).toCbor());
    file.close();

    QVERIFY(!store.loadBucket(kAug, QStringLiteral("cal")).has_value());
}

void TestEventCacheStore::loadCalendarsBeforeAnyStoreReturnsNullopt()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    QVERIFY(!store.loadCalendars().has_value());
}

void TestEventCacheStore::loadCalendarsIgnoresCorruptFile()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    QVERIFY(QDir().mkpath(accountDir));
    QFile file(accountDir + QStringLiteral("/calendars.cbor"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not cbor at all");
    file.close();

    QVERIFY(!store.loadCalendars().has_value());
}

void TestEventCacheStore::loadCalendarsWithWrongSchemaVersionIsIgnored()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    QVERIFY(QDir().mkpath(accountDir));

    const QCborMap map{{QLatin1String("v"), 999}, {QLatin1String("calendars"), QCborArray()}};
    QFile file(accountDir + QStringLiteral("/calendars.cbor"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QCborValue(map).toCbor());
    file.close();

    QVERIFY(!store.loadCalendars().has_value());
}

void TestEventCacheStore::skipsWriteWhenCalendarListUnchanged()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));
    Calendar a;
    a.id = QStringLiteral("a");
    a.summary = QStringLiteral("Work");
    store.storeCalendars({a});

    const QString path = tmp.path() + QStringLiteral("/acct/calendars.cbor");
    const QDateTime marker = QDateTime::currentDateTimeUtc().addSecs(-3600);
    backdate(path, marker);

    store.storeCalendars({a}); // identical -> must not rewrite
    QCOMPARE(QFileInfo(path).lastModified().toUTC().toSecsSinceEpoch(), marker.toSecsSinceEpoch());

    Calendar b;
    b.id = QStringLiteral("b");
    b.summary = QStringLiteral("Home");
    store.storeCalendars({a, b}); // changed -> rewrites
    QVERIFY(QFileInfo(path).lastModified().toUTC().toSecsSinceEpoch() > marker.toSecsSinceEpoch());
}

void TestEventCacheStore::removeCalendarBeforeAnyStoreIsANoOp()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct")); // enabled, but nothing was ever stored

    store.removeCalendar(QStringLiteral("a")); // must not crash
}

void TestEventCacheStore::pruneBeforeAnyStoreIsANoOp()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    store.prune(30, 5); // must not crash
}

void TestEventCacheStore::storeBucketFailsGracefullyWhenAccountDirIsBlockedByAFile()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    // A plain file sitting exactly where the account directory needs to be
    // created makes QDir::mkpath() fail.
    QFile blocker(tmp.path() + QStringLiteral("/acct"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    store.storeBucket(kAug, QStringLiteral("cal"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("cal"))});

    QVERIFY(!store.loadBucket(kAug, QStringLiteral("cal")).has_value());
}

void TestEventCacheStore::storeBucketFailsGracefullyWhenCommitCannotReplaceADirectory()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());
    store.setAccountKey(QStringLiteral("acct"));

    // The account directory itself is fine, but a directory sits exactly at
    // the bucket file's own path, so QSaveFile::commit()'s rename-into-place
    // fails.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayLiteral("cal"), QCryptographicHash::Sha1).toHex());
    const QString accountDir = tmp.path() + QStringLiteral("/acct");
    QVERIFY(QDir().mkpath(accountDir + QStringLiteral("/2026-08__%1.cbor").arg(hash)));

    store.storeBucket(kAug, QStringLiteral("cal"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("cal"))});

    QVERIFY(!store.loadBucket(kAug, QStringLiteral("cal")).has_value());
}

void TestEventCacheStore::accountKeyNamespacesTheCache()
{
    QTemporaryDir tmp;
    EventCacheStore store(tmp.path());

    store.setAccountKey(QStringLiteral("acct1"));
    store.storeBucket(kAug, QStringLiteral("a"), {allDayEvent(QStringLiteral("e1"), QStringLiteral("a"))});

    store.setAccountKey(QStringLiteral("acct2"));
    QVERIFY(!store.loadBucket(kAug, QStringLiteral("a")).has_value());

    store.setAccountKey(QStringLiteral("acct1"));
    QVERIFY(store.loadBucket(kAug, QStringLiteral("a")).has_value());
}

QTEST_GUILESS_MAIN(TestEventCacheStore)
#include "test_eventcachestore.moc"
