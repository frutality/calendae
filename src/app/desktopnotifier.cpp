#include "desktopnotifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLocale>
#include <QLoggingCategory>
#include <QVariantMap>

namespace {
Q_LOGGING_CATEGORY(lcNotify, "tgc.notify")

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
    , m_busAvailable(QDBusConnection::sessionBus().isConnected())
{
    if (!m_busAvailable)
        qCWarning(lcNotify) << "no session D-Bus connection; desktop notifications are disabled";
}

void DesktopNotifier::notify(const DueReminder &reminder)
{
    if (!m_busAvailable) {
        qCWarning(lcNotify) << "notify() called but no session D-Bus; dropping" << reminder.title;
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
    const bool queued = QDBusConnection::sessionBus().send(message);
    qCDebug(lcNotify) << "dispatched notification" << title << "queued=" << queued;
}
