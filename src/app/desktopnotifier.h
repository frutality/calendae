#ifndef DESKTOPNOTIFIER_H
#define DESKTOPNOTIFIER_H

#include "calendar/reminderschedule.h"

#include <QObject>

// Turns a due reminder into a native desktop notification via the
// org.freedesktop.Notifications D-Bus service (the standard Linux
// notification mechanism used by GNOME, KDE, etc.). No system-tray icon is
// involved. A no-op, with a warning logged, when no session bus is
// available.
class DesktopNotifier : public QObject
{
    Q_OBJECT
public:
    explicit DesktopNotifier(QObject *parent = nullptr);

public slots:
    void notify(const DueReminder &reminder);

private:
    bool m_busAvailable;
};

#endif // DESKTOPNOTIFIER_H
