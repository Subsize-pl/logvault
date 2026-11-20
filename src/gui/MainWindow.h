#pragma once

#include "DatabaseJobs.h"
#include "Exporter.h"
#include "FileImporter.h"
#include "SearchService.h"
#include "Settings.h"
#include "TcpServer.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

class FilterBar;
class HistogramWidget;
class Ingestor;
class LiveUpdater;
class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QMessageBox;
class QPushButton;
class QSettings;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTableView;
class QThread;
class QTimer;
class RecordDetailsPanel;
class RecordTableModel;
class ReceiverPanel;
class SourcesPanel;
class StatsView;
class TaskStatus;

/// The application's main window (DESIGN.md section 2): menus, panels, the filters and the records table.
/// It wires the widgets to the core objects and owns the threads of the open database; the work itself is
/// done by the core.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// The settings are read from the store, and changes to them, the recent databases and the window
    /// layout are written to it.
    explicit MainWindow(QSettings& store, QWidget* parent = nullptr);
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
    QDockWidget* addPanel(const QString& name, const QString& title, QWidget* panel, Qt::DockWidgetArea area);
    void createMenus();
    void createToolBar();
    void createCentralArea();
    QWidget* createPlaceholder();
    QWidget* createWorkspace();
    QTableView* createRecordsTable();
    void createStatusBar();
    void connectWidgets();
    void restoreLayout();
    void saveLayout();

    void newDatabase();
    void chooseDatabase();
    void openRecent(const QString& path);
    void openDatabase(const QString& path);
    void switchDatabase(const QString& path);
    void closeDatabase();
    void setRecent(const QStringList& paths);
    void showRecent();
    void startIntake(const QString& path);
    void stopIntake();

    void toggleReceiver();
    void startReceiver();
    void showReceiverStats(const ReceiverStats& stats);
    void updateReceiverAction();
    void showReceiverError(const QString& message);
    void showIntakeError(const QString& message);

    void chooseImportFiles();
    void importFiles(const QStringList& paths);
    void showImportProgress(int percent, const QString& fileName);
    void showImportSummary(const ImportSummary& summary);
    bool canImport() const;
    /// Asks whether to cancel a running import or export; true when none runs or the user agrees.
    bool confirmCancellingTasks(const QString& title, const QString& proceedText);

    void renameSource();
    void deleteSource();
    void deleteOldRecords();
    void showJob(const QString& text, bool cancellable);
    void showJobResult(const DatabaseJobResult& result);

    void chooseExportFile();
    void showExportSummary(const ExportSummary& summary);

    SearchRequest currentFilters() const;
    void search();
    void showSearchResult(const SearchResult& result);

    void showSelectedRecord();
    void copySelectedRawLines();
    void showColumnMenu(const QPoint& position);
    void updateLivePause();
    void updateLiveIndicator();
    void resumeLive();

    void showSettings(bool receiverFirst);
    void applySettings(const Settings& settings);
    void applyTheme();
    void applyTimeZone();
    void syncStore();
    void showAbout();
    void updateActions();
    void warn(const QString& title, const QString& text, const QString& advice);
    void reportError(const QString& message, const QString& advice = {});
    QString dialogDirectory() const;
    QString databaseFilter() const;

    QSettings& m_store;
    Settings m_settings;
    QStringList m_recent; // the recent databases, newest first

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_importAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_renameSourceAction = nullptr;
    QAction* m_deleteSourceAction = nullptr;
    QAction* m_deleteOldAction = nullptr;
    QAction* m_receiverAction = nullptr;
    QAction* m_liveAction = nullptr;
    QAction* m_refreshAction = nullptr;
    QAction* m_findAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_receiverSettingsAction = nullptr;
    QActionGroup* m_themeActions = nullptr;
    QAction* m_aboutAction = nullptr;
    QMenu* m_recentMenu = nullptr;

    QDockWidget* m_sourcesDock = nullptr;
    QDockWidget* m_receiverDock = nullptr;
    QDockWidget* m_detailsDock = nullptr;
    SourcesPanel* m_sourcesPanel = nullptr;
    ReceiverPanel* m_receiverPanel = nullptr;
    RecordDetailsPanel* m_detailsPanel = nullptr;

    QStackedWidget* m_pages = nullptr;
    QWidget* m_placeholderPage = nullptr;
    QWidget* m_recentList = nullptr; // the recent databases on the placeholder page
    QWidget* m_workspacePage = nullptr;
    FilterBar* m_filterBar = nullptr;
    HistogramWidget* m_histogram = nullptr;
    QSplitter* m_splitter = nullptr; // the histogram above the tabs
    QTabWidget* m_tabs = nullptr;
    QTableView* m_table = nullptr;
    StatsView* m_statsView = nullptr;
    RecordTableModel* m_records;

    QLabel* m_searchStatus = nullptr;
    TaskStatus* m_jobTask = nullptr;
    TaskStatus* m_importTask = nullptr;
    TaskStatus* m_exportTask = nullptr;
    QLabel* m_liveStatus = nullptr;
    QMessageBox* m_errorBox = nullptr;
    QString m_errorAdvice; // what to do about the error the box shows
    int m_errorsSinceShown = 0;
    bool m_storeErrorShown = false;

    SearchService* m_searchService;
    DatabaseJobs* m_jobs;
    FileImporter* m_importer;
    Exporter* m_exporter;
    LiveUpdater* m_live;
    QTimer* m_searchingTimer;

    // The intake thread and its objects exist while a database is open; the objects belong to the thread.
    QThread* m_intakeThread = nullptr;
    Ingestor* m_ingestor = nullptr;
    TcpServer* m_server = nullptr;

    QString m_databasePath; // empty while no database is open
    bool m_closing = false; // the window is closing: results of cancelled work are ignored
    bool m_receiverListening = false;
    bool m_receiverStarting = false; // a start was asked for and its outcome has not arrived yet
    QString m_jobSource;             // the source a rename or deletion works on
};
