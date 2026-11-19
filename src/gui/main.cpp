#include "AppInfo.h"
#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(AppInfo::name());
    QApplication::setApplicationVersion(AppInfo::version());
    // Fusion looks the same on every Windows version and is fully recolored by a QPalette (themes).
    QApplication::setStyle(QStringLiteral("Fusion"));

    MainWindow window;
    window.show();
    return QApplication::exec();
}
