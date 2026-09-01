#include "desktopnotifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLocale>
#include <QLoggingCategory>
#include <QVariantMap>

namespace {
Q_LOGGING_CATEGORY(lcNotify, "tgc.notify")

// Our own named connection, opened lazily and retried on each use. The
// default QDBusConnection::sessionBus() caches its first connect attempt
// for the whole process, so a failure at startup (autostarted before the
// session bus / user session was ready) would disable notifications
// forever.
const QString kBusName = QStringLiteral("calendae-notify");

QString leadTimePhrase(int minutesBefore)
{
    if (minutesBefore <= 0)
        return DesktopNotifier::tr("now");
    if (minutesBefore % 1440 == 0) {
        const int days = minutesBefore / 1440;
        return DesktopNotifier::tr("in %n day(s)", nullptr, days);
    }
    if (minutesBefore % 60 == 0) {
        const int hours = minutesBefore / 60;
        return DesktopNotifier::tr("in %n hour(s)", nullptr, hours);
    }
    return DesktopNotifier::tr("in %n minute(s)", nullptr, minutesBefore);
}
} // namespace

DesktopNotifier::DesktopNotifier(QObject *parent)
    : QObject(parent)
{
}

DesktopNotifier::~DesktopNotifier()
{
    QDBusConnection::disconnectFromBus(kBusName);
}

QDBusConnection DesktopNotifier::sessionBus()
{
    // The process's default session bus, if it did connect, is fine to use.
    QDBusConnection defaultBus = QDBusConnection::sessionBus();
    if (defaultBus.isConnected())
        return defaultBus;

    // Otherwise (re)open our own — a fresh connect attempt each time, so a
    // bus that only came up after startup is picked up on the next reminder.
    if (QDBusConnection existing(kBusName); existing.isConnected())
        return existing;
    QDBusConnection::disconnectFromBus(kBusName); // drop any dead handle first
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus, kBusName);
}

void DesktopNotifier::notify(const DueReminder &reminder)
{
    QDBusConnection bus = sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcNotify) << "no session D-Bus; dropping notification" << reminder.title;
        return;
    }

    const QString title = reminder.title.isEmpty() ? tr("(No title)") : reminder.title;

    QString when;
    if (reminder.allDay) {
        when = tr("All day, %1")
                   .arg(QLocale::system().toString(reminder.startLocal.date(), QLocale::ShortFormat));
    } else {
        when = tr("Starts at %1")
                   .arg(QLocale::system().toString(reminder.startLocal.time(), QLocale::ShortFormat));
    }
    const QString body = tr("%1 (%2)").arg(when, leadTimePhrase(reminder.minutesBefore));

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    message << QStringLiteral("Calendae")            // app_name
            << uint(0)                               // replaces_id
            << QStringLiteral("calendae")            // app_icon
            << title                                 // summary
            << body                                  // body
            << QStringList()                         // actions
            << QVariantMap{{QStringLiteral("urgency"), uchar(1)}} // hints (normal urgency)
            << int(0);                               // expire_timeout: 0 = never expire, stays until dismissed

    // Fire-and-forget: the Notify reply is just a notification id we have no
    // use for, so send without waiting for it.
    const bool queued = bus.send(message);
    qCDebug(lcNotify) << "dispatched notification" << title << "queued=" << queued;
}
