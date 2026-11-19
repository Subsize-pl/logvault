#include "MainWindow.h"

#include "AppInfo.h"

#include <QLabel>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(AppInfo::name());

    auto *placeholder = new QLabel(tr("No database is open"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    // Small enough to fit a 1366x768 laptop screen together with the title bar and the taskbar.
    resize(1024, 640);
}
