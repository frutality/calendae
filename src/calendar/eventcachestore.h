#ifndef EVENTCACHESTORE_H
#define EVENTCACHESTORE_H

#include "calendar.h"
#include "event.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

// On-disk mirror of MonthEventStore's in-memory bucket cache, plus the last
// known calendar list. Its whole job is to let a cold start (or a fetch that
// fails while offline) paint the last-loaded data immediately instead of a
// blank grid; the network fetch still always runs and overwrites.
//
// Storage layout, under <CacheLocation>/events/ (overridable for tests):
//   <accountKey>/<YYYY>-<MM>__<sha1(calendarId)>.cbor   one file per bucket
//   <accountKey>/calendars.cbor                          last calendar list
// accountKey namespaces the cache per signed-in account (see
// AuthManager::accountKey); with no account set every operation is a no-op.
//
// Each file carries a schemaVersion; a file written by an incompatible
// version (or one that fails to parse) is treated as absent — the caller
// just re-fetches. Writes are atomic (QSaveFile) and skipped entirely when
// the serialized bytes match what's already on disk, so paging around the
// calendar doesn't churn the disk. "fetchedAt" is the file's modification
// time, not a stored field, so an unchanged re-fetch leaves it untouched.
class EventCacheStore
{
public:
    struct CachedBucket
    {
        QList<Event> events;
        QDateTime fetchedAt; // file mtime: when these events last actually changed on disk
    };

    // baseDir empty -> <CacheLocation>/events. Pass an explicit path in tests.
    explicit EventCacheStore(QString baseDir = QString());

    // Empty key disables the store (all reads return nullopt, all writes and
    // removes are no-ops). Set once the signed-in account is known.
    void setAccountKey(const QString &accountKey);
    QString accountKey() const { return m_accountKey; }
    bool enabled() const { return !m_accountKey.isEmpty(); }

    std::optional<CachedBucket> loadBucket(const QDate &monthKey, const QString &calendarId) const;
    void storeBucket(const QDate &monthKey, const QString &calendarId, const QList<Event> &events);

    std::optional<QList<Calendar>> loadCalendars() const;
    void storeCalendars(const QList<Calendar> &calendars);

    // Mirror MonthEventStore::invalidateCalendar / invalidateAll.
    void removeCalendar(const QString &calendarId);
    void removeAll();

    // Drop bucket files older than maxAgeDays; if more than maxBuckets
    // remain, drop the oldest until maxBuckets are left. calendars.cbor is
    // never pruned. Call once at startup.
    void prune(int maxAgeDays, int maxBuckets) const;

private:
    QString accountDir() const;
    QString bucketFilePath(const QDate &monthKey, const QString &calendarId) const;
    QString calendarsFilePath() const;
    static QString calendarIdHash(const QString &calendarId);
    static QDate normalizeMonth(const QDate &monthKey);
    static bool writeFileAtomically(const QString &path, const QByteArray &bytes);

    QString m_baseDir;
    QString m_accountKey;
};

#endif // EVENTCACHESTORE_H
