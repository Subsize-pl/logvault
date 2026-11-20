#include "AppInfo.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QStyleHints>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(AppInfo::name());
    QApplication::setApplicationVersion(AppInfo::version());
    // Fusion looks the same on every Windows version and is fully recolored by a QPalette (themes).
    QApplication::setStyle(QStringLiteral("Fusion"));
    // The Windows accent color would tint the selection and every other row; neutral ones keep the level
    // colors readable. Queued, so a switch between light and dark Windows themes has updated the palette.
    QApplication::setPalette(Theme::neutralPalette(QApplication::palette()));
    QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
        [] { QApplication::setPalette(Theme::neutralPalette(QApplication::palette())); },
        Qt::QueuedConnection);

    MainWindow window;
    window.show();
    return QApplication::exec();
}
