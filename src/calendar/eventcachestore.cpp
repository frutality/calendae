#include "eventcachestore.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
Q_LOGGING_CATEGORY(lcCache, "tgc.eventcache")

// Bump on any incompatible change to Event::toCbor / Calendar::toCbor or the
// wrapper maps below. Older files are then ignored (re-fetched), not parsed.
constexpr int kSchemaVersion = 1;

const QLatin1String kFieldVersion("v");
const QLatin1String kFieldCalendarId("calendarId");
const QLatin1String kFieldMonthKey("monthKey");
const QLatin1String kFieldEvents("events");
const QLatin1String kFieldCalendars("calendars");
} // namespace

EventCacheStore::EventCacheStore(QString baseDir)
    : m_baseDir(baseDir.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/events")
                    : std::move(baseDir))
{
}

void EventCacheStore::setAccountKey(const QString &accountKey)
{
    m_accountKey = accountKey;
}

QDate EventCacheStore::normalizeMonth(const QDate &monthKey)
{
    return monthKey.isValid() ? QDate(monthKey.year(), monthKey.month(), 1) : QDate();
}

QString EventCacheStore::calendarIdHash(const QString &calendarId)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(calendarId.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString EventCacheStore::accountDir() const
{
    if (m_accountKey.isEmpty())
        return QString();
    return m_baseDir + QLatin1Char('/') + m_accountKey;
}

QString EventCacheStore::bucketFilePath(const QDate &monthKey, const QString &calendarId) const
{
    const QDate month = normalizeMonth(monthKey);
    return accountDir()
        + QStringLiteral("/%1__%2.cbor")
              .arg(month.toString(QStringLiteral("yyyy-MM")), calendarIdHash(calendarId));
}

QString EventCacheStore::calendarsFilePath() const
{
    return accountDir() + QStringLiteral("/calendars.cbor");
}

bool EventCacheStore::writeFileAtomically(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(lcCache) << "could not create cache directory for" << path;
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcCache) << "could not open" << path << "for writing:" << file.errorString();
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        qCWarning(lcCache) << "could not commit" << path << ":" << file.errorString();
        return false;
    }
    return true;
}

std::optional<EventCacheStore::CachedBucket> EventCacheStore::loadBucket(const QDate &monthKey,
                                                                        const QString &calendarId) const
{
    if (!enabled())
        return std::nullopt;

    const QString path = bucketFilePath(monthKey, calendarId);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    const QCborValue root = QCborValue::fromCbor(file.readAll());
    file.close();
    if (!root.isMap())
        return std::nullopt;

    const QCborMap map = root.toMap();
    if (map.value(kFieldVersion).toInteger() != kSchemaVersion) {
        qCDebug(lcCache) << "ignoring" << path << "- schema version mismatch";
        return std::nullopt;
    }

    CachedBucket bucket;
    bucket.fetchedAt = QFileInfo(path).lastModified();
    const QCborArray events = map.value(kFieldEvents).toArray();
    bucket.events.reserve(int(events.size()));
    for (const QCborValue &value : events) {
        if (const std::optional<Event> event = Event::fromCbor(value.toMap()))
            bucket.events.append(*event);
    }
    return bucket;
}

void EventCacheStore::storeBucket(const QDate &monthKey, const QString &calendarId,
                                  const QList<Event> &events)
{
    if (!enabled())
        return;

    QCborArray serializedEvents;
    for (const Event &event : events)
        serializedEvents.append(event.toCbor());

    const QCborMap map{
        {kFieldVersion, kSchemaVersion},
        {kFieldCalendarId, calendarId},
        {kFieldMonthKey, normalizeMonth(monthKey).toString(Qt::ISODate)},
        {kFieldEvents, serializedEvents},
    };
    const QByteArray bytes = QCborValue(map).toCbor();

    const QString path = bucketFilePath(monthKey, calendarId);
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes)
        return; // unchanged — don't touch the file (keeps its mtime / avoids disk churn)
    existing.close();

    writeFileAtomically(path, bytes);
}

std::optional<QList<Calendar>> EventCacheStore::loadCalendars() const
{
    if (!enabled())
        return std::nullopt;

    QFile file(calendarsFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    const QCborValue root = QCborValue::fromCbor(file.readAll());
    if (!root.isMap())
        return std::nullopt;

    const QCborMap map = root.toMap();
    if (map.value(kFieldVersion).toInteger() != kSchemaVersion)
        return std::nullopt;

    QList<Calendar> calendars;
    const QCborArray items = map.value(kFieldCalendars).toArray();
    for (const QCborValue &value : items) {
        if (const std::optional<Calendar> calendar = Calendar::fromCbor(value.toMap()))
            calendars.append(*calendar);
    }
    return calendars;
}

void EventCacheStore::storeCalendars(const QList<Calendar> &calendars)
{
    if (!enabled())
        return;

    QCborArray serialized;
    for (const Calendar &calendar : calendars)
        serialized.append(calendar.toCbor());

    const QCborMap map{
        {kFieldVersion, kSchemaVersion},
        {kFieldCalendars, serialized},
    };
    const QByteArray bytes = QCborValue(map).toCbor();

    const QString path = calendarsFilePath();
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes)
        return;
    existing.close();

    writeFileAtomically(path, bytes);
}

void EventCacheStore::removeCalendar(const QString &calendarId)
{
    if (!enabled())
        return;

    QDir dir(accountDir());
    if (!dir.exists())
        return;
    const QStringList files = dir.entryList(
        {QStringLiteral("*__%1.cbor").arg(calendarIdHash(calendarId))}, QDir::Files);
    for (const QString &name : files)
        dir.remove(name);
}

void EventCacheStore::removeAll()
{
    if (!enabled())
        return;
    QDir(accountDir()).removeRecursively();
}

void EventCacheStore::prune(int maxAgeDays, int maxBuckets) const
{
    if (!enabled())
        return;

    QDir dir(accountDir());
    if (!dir.exists())
        return;

    QFileInfoList buckets = dir.entryInfoList({QStringLiteral("*__*.cbor")}, QDir::Files, QDir::Time);
    // QDir::Time sorts newest-first; walk oldest-first for age + count trimming.
    std::reverse(buckets.begin(), buckets.end());

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays);
    for (auto it = buckets.begin(); it != buckets.end();) {
        if (it->lastModified() < cutoff) {
            QFile::remove(it->absoluteFilePath());
            it = buckets.erase(it);
        } else {
            ++it;
        }
    }

    for (int i = 0; buckets.size() - i > maxBuckets; ++i)
        QFile::remove(buckets.at(i).absoluteFilePath());
}
