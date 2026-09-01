#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
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

    QTranslator appTranslator;
    QTranslator qtTranslator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString localeName = QLocale(locale).name();
        if (appTranslator.load(":/i18n/calendae_" + localeName)) {
            a.installTranslator(&appTranslator);
            // Qt's own strings for the same locale — the standard
            // QMessageBox / QDialogButtonBox buttons, the QLineEdit context
            // menu, etc. Best-effort: a given Qt install may not ship this
            // locale's qtbase_*.qm.
            if (qtTranslator.load("qtbase_" + localeName,
                                   QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
                a.installTranslator(&qtTranslator);
            }
            break;
        }
    }
    MainWindow w;
    w.show();
    return QApplication::exec();
}
