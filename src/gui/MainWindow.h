#pragma once

#include "DatabaseJobs.h"
#include "FileImporter.h"
#include "SearchService.h"
#include "TcpServer.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

class FilterBar;
class Ingestor;
class LiveUpdater;
class QAction;
class QDockWidget;
class QLabel;
class QMessageBox;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableView;
class QThread;
class QTimer;
class RecordDetailsPanel;
class RecordTableModel;
class ReceiverPanel;
class SourcesPanel;

/// The application's main window (DESIGN.md section 2): menus, panels, the filters and the records table.
/// It wires the widgets to the core objects and owns the threads of the open database; the work itself is
/// done by the core.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    /// Closes the database in the order of DESIGN.md section 3.3.
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createActions();
    void createDocks();
    QDockWidget* addPanel(const QString& title, QWidget* panel, Qt::DockWidgetArea area);
    void createMenus();
    void createToolBar();
    void createCentralArea();
    QWidget* createPlaceholder();
    QWidget* createWorkspace();
    QTableView* createRecordsTable();
    void createStatusBar();
    void connectWidgets();

    void newDatabase();
    void chooseDatabase();
    void openDatabase(const QString& path);
    void switchDatabase(const QString& path);
    void closeDatabase();
    void startIntake(const QString& path);
    void stopIntake();

    void toggleReceiver();
    void startReceiver();
    void showReceiverStats(const ReceiverStats& stats);
    void showReceiverError(const QString& message);

    void chooseImportFiles();
    void importFiles(const QStringList& paths);
    void showImportProgress(int percent, const QString& fileName);
    void showImportSummary(const ImportSummary& summary);
    bool canImport() const;

    void renameSource();
    void deleteSource();
    void showJob(const QString& text, bool cancellable);
    void showJobResult(const DatabaseJobResult& result);

    void search();
    void showSearchResult(const SearchResult& result);

    void showSelectedRecord();
    void copySelectedRawLines();
    void showColumnMenu(const QPoint& position);
    void updateLivePause();
    void updateLiveIndicator();
    void resumeLive();

    void updateActions();
    void reportError(const QString& message);
    QString dialogDirectory() const;
    QString databaseFilter() const;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_importAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_renameSourceAction = nullptr;
    QAction* m_deleteSourceAction = nullptr;
    QAction* m_receiverAction = nullptr;
    QAction* m_liveAction = nullptr;
    QAction* m_refreshAction = nullptr;
    QAction* m_findAction = nullptr;

    QDockWidget* m_sourcesDock = nullptr;
    QDockWidget* m_receiverDock = nullptr;
    QDockWidget* m_detailsDock = nullptr;
    SourcesPanel* m_sourcesPanel = nullptr;
    ReceiverPanel* m_receiverPanel = nullptr;
    RecordDetailsPanel* m_detailsPanel = nullptr;

    QStackedWidget* m_pages = nullptr;
    QWidget* m_placeholderPage = nullptr;
    QWidget* m_workspacePage = nullptr;
    FilterBar* m_filterBar = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableView* m_table = nullptr;
    RecordTableModel* m_records;

    QLabel* m_searchStatus = nullptr;
    QLabel* m_jobStatus = nullptr;
    QPushButton* m_jobCancel = nullptr;
    QLabel* m_importStatus = nullptr;
    QProgressBar* m_importProgress = nullptr;
    QPushButton* m_importCancel = nullptr;
    QLabel* m_liveStatus = nullptr;
    QMessageBox* m_errorBox = nullptr;
    int m_errorsSinceShown = 0;

    SearchService* m_searchService;
    DatabaseJobs* m_jobs;
    FileImporter* m_importer;
    LiveUpdater* m_live;
    QTimer* m_searchingTimer;

    // The intake thread and its objects exist while a database is open; the objects belong to the thread.
    QThread* m_intakeThread = nullptr;
    Ingestor* m_ingestor = nullptr;
    TcpServer* m_server = nullptr;

    QString m_databasePath; // empty while no database is open
    bool m_receiverListening = false;
    bool m_receiverStarting = false; // a start was asked for and its outcome has not arrived yet
    QString m_jobSource;             // the source a rename or deletion works on
};
