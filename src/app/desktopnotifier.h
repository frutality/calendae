#ifndef DESKTOPNOTIFIER_H
#define DESKTOPNOTIFIER_H

#include "calendar/reminderschedule.h"

#include <QDBusConnection>
#include <QObject>

// Turns a due reminder into a native desktop notification via the
// org.freedesktop.Notifications D-Bus service (the standard Linux
// notification mechanism used by GNOME, KDE, etc.). No system-tray icon is
// involved. A no-op, with a warning logged, when no session bus is
// available — re-checked on every notification, so a process autostarted
// before the session bus was up still delivers once it comes up.
class DesktopNotifier : public QObject
{
    Q_OBJECT
public:
    explicit DesktopNotifier(QObject *parent = nullptr);
    ~DesktopNotifier() override;

public slots:
    void notify(const DueReminder &reminder);

private:
    // A connected session bus, reconnecting if the process's default one
    // never came up. Not cached: QDBusConnection::sessionBus() latches its
    // first (possibly failed) attempt for the process lifetime.
    QDBusConnection sessionBus();
};

#endif // DESKTOPNOTIFIER_H
