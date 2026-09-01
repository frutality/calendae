#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("calendae"));
    QApplication::setApplicationDisplayName(QStringLiteral("Calendae"));
    // Ties the process/window to calendae.desktop so the KDE task manager and
    // System Monitor can resolve an icon for it (and sets WM_CLASS on X11).
    QApplication::setDesktopFileName(QStringLiteral("calendae"));

    // Fall back to the copies baked into the Qt resource so a dev build run
    // straight from ./build still has a window icon without `make install`;
    // once installed, the themed "calendae" icon (hicolor) takes over.
    QIcon fallbackIcon(QStringLiteral(":/icons/calendae-256.png"));
    for (int size : {16, 24, 32, 48, 64, 128})
        fallbackIcon.addFile(QStringLiteral(":/icons/calendae-%1.png").arg(size));
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("calendae"), fallbackIcon));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "calendae_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return QApplication::exec();
}
