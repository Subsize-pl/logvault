#pragma once

#include <QMainWindow>

/// The application's main window. While no database is open, its center shows a placeholder.
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
};
