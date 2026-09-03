#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QPixmapCache>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("calendae"));
    QApplication::setApplicationDisplayName(QStringLiteral("Calendae"));
    // Ties the process/window to calendae.desktop so the KDE task manager and
    // System Monitor can resolve an icon for it (and sets WM_CLASS on X11).
    QApplication::setDesktopFileName(QStringLiteral("calendae"));

    // Window icon straight from the baked-in Qt resource. Deliberately not
    // QIcon::fromTheme(): the first fromTheme() call anywhere constructs
    // QIconLoader and parses the entire current desktop icon theme's index
    // (Breeze/Adwaita index.theme is tens of KB and thousands of entries) —
    // pure overhead here. The KDE/GNOME task manager still shows the
    // installed themed "calendae" icon via setDesktopFileName() above; the
    // in-process icon only needs to cover the title bar and Alt-Tab, and
    // these bundled PNGs are the same artwork.
    QIcon appIcon(QStringLiteral(":/icons/calendae-256.png"));
    for (int size : {16, 24, 32, 48, 64, 128})
        appIcon.addFile(QStringLiteral(":/icons/calendae-%1.png").arg(size));
    QApplication::setWindowIcon(appIcon);

    // This app renders no bitmap imagery beyond its own small icon, so the
    // default 10 MB QPixmapCache ceiling is just headroom for growth that
    // never gets reclaimed. 2 MB is comfortably above what QStyle caches
    // for a window this size. Glyph and style caches are separate and
    // untouched by this.
    QPixmapCache::setCacheLimit(2048); // KiB

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
