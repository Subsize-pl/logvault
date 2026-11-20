#include "AppInfo.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QStyleHints>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(AppInfo::name());
    QApplication::setApplicationName(AppInfo::name());
    QApplication::setApplicationVersion(AppInfo::version());
    // Every window and the About box show it; the .exe has the same icon from LogVault.rc.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/logvault.ico")));
    // %APPDATA%\LogVault\LogVault.ini rather than the registry: easy to read, copy or delete.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings store;
    // Fusion looks the same on every Windows version and is fully recolored by a QPalette (themes).
    QApplication::setStyle(QStringLiteral("Fusion"));
    // The Windows accent color would tint the selection and every other row; neutral ones keep the level
    // colors readable. Queued, so a switch between light and dark Windows themes has updated the palette.
    QApplication::setPalette(Theme::neutralPalette(QApplication::palette()));
    QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
        [] { QApplication::setPalette(Theme::neutralPalette(QApplication::palette())); },
        Qt::QueuedConnection);

    MainWindow window(store);
    window.show();
    return QApplication::exec();
}
