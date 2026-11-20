#include "MainWindow.h"

#include "AppInfo.h"
#include "DeleteRecordsDialog.h"
#include "FilterBar.h"
#include "Format.h"
#include "HistogramWidget.h"
#include "ImportDialog.h"
#include "Ingestor.h"
#include "LiveUpdater.h"
#include "ReceiverPanel.h"
#include "RecordDetailsPanel.h"
#include "RecordTableModel.h"
#include "SourcesPanel.h"
#include "StatsView.h"
#include "TaskStatus.h"
#include "Theme.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <initializer_list>

using namespace Qt::StringLiterals;

namespace {

// Defaults of the settings in DESIGN.md section 1.2 (F16): the receiver listens on this computer only, on
// the port the Python client uses, and the table shows the newest 10,000 matches.
constexpr QHostAddress::SpecialAddress kReceiverAddress = QHostAddress::LocalHost;
constexpr quint16 kReceiverPort = 5170;
constexpr int kMaxRows = 10'000;

// A search that takes longer shows "Searching..."; for faster ones the text would only flicker.
constexpr int kSearchingDelayMs = 300;
constexpr int kMessageTimeoutMs = 5000;
constexpr int kHistogramHeight = 120;

constexpr QChar kDot(0x25CF);
constexpr QChar kEmDash(0x2014);
constexpr QChar kMiddleDot(0x00B7);

QString coloredDot(const QColor& color) {
    return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color.name(), QString(kDot));
}

// The files among dragged items; folders and anything that is not a local file are left out.
QStringList localFiles(const QMimeData* data) {
    QStringList paths;
    for (const QUrl& url : data->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
            paths.append(url.toLocalFile());
        }
    }
    return paths;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_records(new RecordTableModel(this)), m_searchService(new SearchService(this)),
      m_jobs(new DatabaseJobs(this)), m_importer(new FileImporter(this)), m_exporter(new Exporter(this)),
      m_live(new LiveUpdater(LiveUpdater::kDefaultIntervalMs, this)), m_searchingTimer(new QTimer(this)) {
    setWindowTitle(AppInfo::name());
    setAcceptDrops(true);
    createActions();
    createDocks();
    createMenus();
    createToolBar();
    createCentralArea();
    createStatusBar();
    connectWidgets();
    showReceiverStats({});
    updateActions();
    updateLiveIndicator();
    // Most of the screen, so the panels and the table have room, but not all of it.
    resize(screen()->availableGeometry().size() * 0.85);
}

MainWindow::~MainWindow() { closeDatabase(); }

void MainWindow::closeEvent(QCloseEvent* event) {
    closeDatabase();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (canImport() && !localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (canImport() && !localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QStringList paths = localFiles(event->mimeData());
    if (!canImport() || paths.isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    // The program the files came from waits until the drop is handled, so the dialog opens afterwards.
    QMetaObject::invokeMethod(this, [this, paths] { importFiles(paths); }, Qt::QueuedConnection);
}

void MainWindow::createActions() {
    m_newAction = new QAction(style()->standardIcon(QStyle::SP_FileIcon), tr("&New Database..."), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newDatabase);

    m_openAction =
        new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open Database..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setIconText(tr("Open"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::chooseDatabase);

    m_importAction = new QAction(Theme::glyphIcon(Theme::Glyph::Import), tr("&Import Files..."), this);
    m_importAction->setShortcut(Qt::CTRL | Qt::Key_I);
    m_importAction->setIconText(tr("Import"));
    connect(m_importAction, &QAction::triggered, this, &MainWindow::chooseImportFiles);

    m_exportAction = new QAction(Theme::glyphIcon(Theme::Glyph::Export), tr("&Export Results..."), this);
    m_exportAction->setShortcut(Qt::CTRL | Qt::Key_E);
    m_exportAction->setIconText(tr("Export"));
    m_exportAction->setToolTip(tr("Export every record of the current search to CSV or JSON Lines"));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::chooseExportFile);

    m_exitAction = new QAction(tr("E&xit"), this);
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    m_renameSourceAction = new QAction(tr("&Rename Source..."), this);
    connect(m_renameSourceAction, &QAction::triggered, this, &MainWindow::renameSource);
    m_deleteSourceAction = new QAction(tr("&Delete Source..."), this);
    connect(m_deleteSourceAction, &QAction::triggered, this, &MainWindow::deleteSource);
    m_deleteOldAction = new QAction(tr("Delete Records &Older Than..."), this);
    connect(m_deleteOldAction, &QAction::triggered, this, &MainWindow::deleteOldRecords);

    // Its text, icon and tooltip follow the receiver's state (showReceiverStats()).
    m_receiverAction = new QAction(this);
    m_receiverAction->setShortcut(Qt::CTRL | Qt::Key_R);
    connect(m_receiverAction, &QAction::triggered, this, &MainWindow::toggleReceiver);

    m_liveAction = new QAction(Theme::glyphIcon(Theme::Glyph::Live), tr("&Live Mode"), this);
    m_liveAction->setIconText(tr("Live"));
    m_liveAction->setToolTip(tr("Show new records as they arrive"));
    m_liveAction->setCheckable(true);
    m_liveAction->setShortcut(Qt::CTRL | Qt::Key_L);

    m_refreshAction = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("&Refresh"), this);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    connect(m_refreshAction, &QAction::triggered, this, &MainWindow::search);

    // In no menu: Ctrl+F jumps to the search line from anywhere in the window.
    m_findAction = new QAction(tr("Find"), this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, [this] { m_filterBar->focusSearchLine(); });
    addAction(m_findAction);
}

void MainWindow::createDocks() {
    m_sourcesPanel = new SourcesPanel({m_renameSourceAction, m_deleteSourceAction});
    m_receiverPanel = new ReceiverPanel(m_receiverAction);
    m_detailsPanel = new RecordDetailsPanel;
    m_sourcesDock = addPanel(tr("Sources"), m_sourcesPanel, Qt::LeftDockWidgetArea);
    m_receiverDock = addPanel(tr("Receiver"), m_receiverPanel, Qt::LeftDockWidgetArea);
    m_detailsDock = addPanel(tr("Record Details"), m_detailsPanel, Qt::RightDockWidgetArea);
}

QDockWidget* MainWindow::addPanel(const QString& title, QWidget* panel, Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, this);
    dock->setWidget(panel);
    addDockWidget(area, dock);
    return dock;
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_newAction);
    file->addAction(m_openAction);
    file->addSeparator();
    file->addAction(m_importAction);
    file->addAction(m_exportAction);
    file->addSeparator();
    file->addAction(m_exitAction);

    QMenu* data = menuBar()->addMenu(tr("&Data"));
    data->addAction(m_renameSourceAction);
    data->addAction(m_deleteSourceAction);
    data->addSeparator();
    data->addAction(m_deleteOldAction);

    QMenu* receiver = menuBar()->addMenu(tr("&Receiver"));
    receiver->addAction(m_receiverAction);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QMenu* panels = view->addMenu(tr("&Panels"));
    panels->addAction(m_sourcesDock->toggleViewAction());
    panels->addAction(m_receiverDock->toggleViewAction());
    panels->addAction(m_detailsDock->toggleViewAction());
    view->addSeparator();
    view->addAction(m_liveAction);
    view->addAction(m_refreshAction);
}

void MainWindow::createToolBar() {
    QToolBar* toolBar = addToolBar(tr("Main"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->addAction(m_openAction);
    toolBar->addAction(m_importAction);
    toolBar->addAction(m_exportAction);
    toolBar->addSeparator();
    toolBar->addAction(m_receiverAction);
    toolBar->addSeparator();
    toolBar->addAction(m_liveAction);
    toolBar->addAction(m_refreshAction);
}

void MainWindow::createCentralArea() {
    m_placeholderPage = createPlaceholder();
    m_workspacePage = createWorkspace();
    m_pages = new QStackedWidget;
    m_pages->addWidget(m_placeholderPage);
    m_pages->addWidget(m_workspacePage);
    setCentralWidget(m_pages);
}

QWidget* MainWindow::createPlaceholder() {
    auto* title = new QLabel(AppInfo::name());
    QFont titleFont = title->font();
    titleFont.setBold(true);
    if (titleFont.pointSizeF() > 0) {
        titleFont.setPointSizeF(titleFont.pointSizeF() * 2);
    }
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    auto* hint = new QLabel(tr("No database is open. Create a new one or open an existing one."));
    hint->setAlignment(Qt::AlignCenter);

    auto* newButton = new QPushButton(m_newAction->icon(), tr("New Database..."));
    auto* openButton = new QPushButton(m_openAction->icon(), tr("Open Database..."));
    connect(newButton, &QPushButton::clicked, m_newAction, &QAction::trigger);
    connect(openButton, &QPushButton::clicked, m_openAction, &QAction::trigger);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(newButton);
    buttons->addWidget(openButton);
    buttons->addStretch();

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addSpacing(16);
    layout->addLayout(buttons);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::createWorkspace() {
    m_filterBar = new FilterBar;
    m_histogram = new HistogramWidget;

    m_table = createRecordsTable();
    m_statsView = new StatsView;
    m_tabs = new QTabWidget;
    m_tabs->addTab(m_table, tr("Records"));
    m_tabs->addTab(m_statsView, tr("Statistics"));

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_histogram);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    // The histogram starts at its height and the tabs, the only part that stretches, take the rest. Left to
    // itself, the splitter would size the histogram from a widget that has not been laid out yet.
    splitter->setSizes({kHistogramHeight, kHistogramHeight});

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(m_filterBar);
    layout->addWidget(splitter, 1);
    return page;
}

QTableView* MainWindow::createRecordsTable() {
    auto* table = new QTableView;
    table->setModel(m_records);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);

    // One line per record and a fixed row height keep 10,000 rows cheap to lay out.
    QHeaderView* rows = table->verticalHeader();
    rows->hide();
    rows->setSectionResizeMode(QHeaderView::Fixed);
    rows->setDefaultSectionSize(
        qMax(table->fontMetrics().height(), QFontMetrics(Theme::monospaceFont()).height()) + 6);

    QFont bold = table->font();
    bold.setBold(true);
    QHeaderView* columns = table->horizontalHeader();
    columns->setSectionsMovable(true);
    columns->setStretchLastSection(true);
    columns->setHighlightSections(false);
    columns->resizeSection(RecordTableModel::TimeColumn,
                           table->fontMetrics().horizontalAdvance(Format::timestamp(0)) + 24);
    columns->resizeSection(RecordTableModel::LevelColumn,
                           QFontMetrics(bold).horizontalAdvance(levelName(Level::Unknown)) + 24);
    columns->resizeSection(RecordTableModel::SourceColumn, table->fontMetrics().averageCharWidth() * 16);
    columns->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(columns, &QHeaderView::customContextMenuRequested, this, &MainWindow::showColumnMenu);

    auto* copy = new QAction(tr("Copy Raw Lines"), table);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QAction::triggered, this, &MainWindow::copySelectedRawLines);
    auto* clearSelection = new QAction(tr("Clear Selection"), table);
    clearSelection->setShortcut(Qt::Key_Escape);
    clearSelection->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(clearSelection, &QAction::triggered, table, &QAbstractItemView::clearSelection);
    table->addActions({copy, clearSelection});
    table->setContextMenuPolicy(Qt::ActionsContextMenu);
    return table;
}

void MainWindow::createStatusBar() {
    m_searchStatus = new QLabel;
    m_jobTask = new TaskStatus;
    m_importTask = new TaskStatus;
    m_exportTask = new TaskStatus;
    m_liveStatus = new QLabel;
    m_liveStatus->setTextFormat(Qt::RichText);
    m_liveStatus->hide();

    statusBar()->addWidget(m_searchStatus, 1);
    for (QWidget* widget :
         std::initializer_list<QWidget*>{m_jobTask, m_importTask, m_exportTask, m_liveStatus}) {
        statusBar()->addPermanentWidget(widget);
    }

    connect(m_jobTask, &TaskStatus::cancelRequested, m_jobs, &DatabaseJobs::cancel);
    connect(m_importTask, &TaskStatus::cancelRequested, m_importer, &FileImporter::cancel);
    connect(m_exportTask, &TaskStatus::cancelRequested, m_exporter, &Exporter::cancel);
    connect(m_liveStatus, &QLabel::linkActivated, this, &MainWindow::resumeLive);
}

void MainWindow::connectWidgets() {
    connect(m_searchService, &SearchService::finished, this, &MainWindow::showSearchResult);
    connect(m_jobs, &DatabaseJobs::finished, this, &MainWindow::showJobResult);
    connect(m_jobs, &DatabaseJobs::progressChanged, m_jobTask, &TaskStatus::setProgress);
    connect(m_exporter, &Exporter::progressChanged, m_exportTask, &TaskStatus::setProgress);
    connect(m_exporter, &Exporter::finished, this, &MainWindow::showExportSummary);
    connect(m_importer, &FileImporter::progressChanged, this, &MainWindow::showImportProgress);
    connect(m_importer, &FileImporter::finished, this, &MainWindow::showImportSummary);

    m_searchingTimer->setSingleShot(true);
    m_searchingTimer->setInterval(kSearchingDelayMs);
    connect(m_searchingTimer, &QTimer::timeout, this,
            [this] { m_searchStatus->setText(tr("Searching...")); });

    connect(m_liveAction, &QAction::toggled, m_live, &LiveUpdater::setEnabled);
    connect(m_live, &LiveUpdater::refreshDue, this, &MainWindow::search);
    connect(m_live, &LiveUpdater::stateChanged, this, &MainWindow::updateLiveIndicator);

    connect(m_filterBar, &FilterBar::searchRequested, this, &MainWindow::search);
    connect(m_histogram, &HistogramWidget::periodSelected, m_filterBar, &FilterBar::zoomToPeriod);
    connect(m_histogram, &HistogramWidget::previousPeriodRequested, m_filterBar,
            &FilterBar::restorePreviousPeriod);

    // Statistics are computed only while their tab is shown (DESIGN.md section 6.5), so showing it searches.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (m_tabs->currentWidget() == m_statsView) {
            search();
        }
    });
    connect(m_statsView, &StatsView::groupingChanged, this, &MainWindow::search);
    connect(m_statsView, &StatsView::levelRequested, m_filterBar, &FilterBar::showOnlyLevel);
    connect(m_statsView, &StatsView::sourceRequested, m_sourcesPanel, &SourcesPanel::showOnlySourceId);
    connect(m_statsView, &StatsView::searchTermRequested, m_filterBar, &FilterBar::addSearchTerm);
    connect(m_sourcesPanel, &SourcesPanel::selectionChanged, this, [this] {
        updateActions();
        search();
    });
    connect(m_detailsPanel, &RecordDetailsPanel::searchTermRequested, m_filterBar, &FilterBar::addSearchTerm);
    connect(m_detailsPanel, &RecordDetailsPanel::levelFilterRequested, this,
            [this](Level level, bool exclude) {
                if (exclude) {
                    m_filterBar->hideLevel(level);
                } else {
                    m_filterBar->showOnlyLevel(level);
                }
            });
    connect(m_detailsPanel, &RecordDetailsPanel::sourceFilterRequested, this,
            [this](const QString& source, bool exclude) {
                if (exclude) {
                    m_sourcesPanel->hideSource(source);
                } else {
                    m_sourcesPanel->showOnlySource(source);
                }
            });

    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        showSelectedRecord();
        updateLivePause();
    });
    // A new result clears the selection without selectionChanged().
    connect(m_records, &QAbstractItemModel::modelReset, this, [this] {
        showSelectedRecord();
        updateLivePause();
    });
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::updateLivePause);
}

void MainWindow::newDatabase() {
    QString path = QFileDialog::getSaveFileName(this, tr("New Database"), dialogDirectory() + u"/logs.lvdb"_s,
                                                databaseFilter(), nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += u".lvdb"_s;
    }
    // A new database never replaces a file, so a slip in the dialog cannot lose data.
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(
            this, tr("New Database"),
            tr("%1 already exists.\n\nChoose another name, or open the file with File > Open "
               "Database.")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }
    openDatabase(path);
}

void MainWindow::chooseDatabase() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open Database"), dialogDirectory(), databaseFilter());
    if (!path.isEmpty()) {
        openDatabase(path);
    }
}

void MainWindow::openDatabase(const QString& path) {
    if (!m_databasePath.isEmpty() && QFileInfo(path) == QFileInfo(m_databasePath)) {
        return;
    }
    // The current database stays open until the new one has been opened and checked in the background.
    if (m_jobs->open(path)) {
        showJob(tr("Opening %1...").arg(QFileInfo(path).fileName()), false);
    }
}

void MainWindow::switchDatabase(const QString& path) {
    const bool receiverWasOn = m_receiverListening;
    closeDatabase();
    m_databasePath = path;
    m_searchService->setDatabasePath(path);
    startIntake(path);
    setWindowTitle(AppInfo::name() + u' ' + kEmDash + u' ' + QFileInfo(path).fileName());
    m_pages->setCurrentWidget(m_workspacePage);
    // Clients keep sending while the user switches databases, so a running receiver goes on with the new one.
    if (receiverWasOn) {
        startReceiver();
    }
    updateActions();
    updateLiveIndicator();
    search();
}

void MainWindow::closeDatabase() {
    if (m_databasePath.isEmpty()) {
        return;
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // DESIGN.md section 3.3: the server stops and the ingestor writes what it holds; imports and jobs are
    // cancelled and the thread pool (searches included) is awaited; the intake thread ends last.
    if (m_intakeThread != nullptr) {
        TcpServer* server = m_server;
        Ingestor* ingestor = m_ingestor;
        QMetaObject::invokeMethod(
            server,
            [server, ingestor] {
                server->stop();
                ingestor->stop();
            },
            Qt::BlockingQueuedConnection);
    }
    m_importer->cancel();
    m_exporter->cancel();
    m_jobs->cancel();
    // Searches for another database are stale: a running one stops after its current query, and its
    // result is dropped.
    m_searchService->setDatabasePath({});
    m_importer->waitForFinished();
    m_exporter->waitForFinished();
    m_jobs->waitForFinished();
    m_searchService->waitForFinished();
    stopIntake();

    m_databasePath.clear();
    m_searchingTimer->stop();
    // The dropped search never reports back, so live mode must not keep waiting for it.
    m_live->searchFinished();
    m_records->setRecords({});
    m_histogram->clear();
    m_statsView->clear();
    m_sourcesPanel->setSources({});
    m_detailsPanel->clear();
    showReceiverStats({});
    m_searchStatus->clear();
    m_tabs->setTabText(m_tabs->indexOf(m_table), tr("Records"));
    m_pages->setCurrentWidget(m_placeholderPage);
    setWindowTitle(AppInfo::name());
    updateActions();
    updateLiveIndicator();
    QGuiApplication::restoreOverrideCursor();
}

void MainWindow::startIntake(const QString& path) {
    // Neither object has a parent: both move to the intake thread, which deletes them when it finishes.
    m_intakeThread = new QThread(this);
    m_ingestor = new Ingestor;
    m_server = new TcpServer;
    m_ingestor->moveToThread(m_intakeThread);
    m_server->moveToThread(m_intakeThread);
    connect(m_intakeThread, &QThread::finished, m_ingestor, &QObject::deleteLater);
    connect(m_intakeThread, &QThread::finished, m_server, &QObject::deleteLater);
    // Both live in the intake thread, so lines are handed over by a direct call; the rest is queued here.
    connect(m_server, &TcpServer::linesReceived, m_ingestor, &Ingestor::addLines);
    connect(m_ingestor, &Ingestor::recordsWritten, m_live, &LiveUpdater::recordsWritten);
    connect(m_ingestor, &Ingestor::errorOccurred, this, &MainWindow::reportError);
    connect(m_server, &TcpServer::errorOccurred, this, &MainWindow::showReceiverError);
    connect(m_server, &TcpServer::statsChanged, this, &MainWindow::showReceiverStats);
    m_intakeThread->start();

    Ingestor* ingestor = m_ingestor;
    QMetaObject::invokeMethod(ingestor, [ingestor, path] { ingestor->start(path); });
}

void MainWindow::stopIntake() {
    if (m_intakeThread == nullptr) {
        return;
    }
    m_intakeThread->quit();
    m_intakeThread->wait();
    m_intakeThread->deleteLater();
    m_intakeThread = nullptr;
    m_ingestor = nullptr;
    m_server = nullptr;
}

void MainWindow::toggleReceiver() {
    if (m_server == nullptr) {
        return;
    }
    if (!m_receiverListening) {
        startReceiver();
        return;
    }
    TcpServer* server = m_server;
    QMetaObject::invokeMethod(server, [server] { server->stop(); });
}

void MainWindow::startReceiver() {
    if (m_server == nullptr) {
        return;
    }
    m_receiverStarting = true;
    TcpServer* server = m_server;
    QMetaObject::invokeMethod(server,
                              [server] { server->start(QHostAddress(kReceiverAddress), kReceiverPort); });
}

void MainWindow::showReceiverStats(const ReceiverStats& stats) {
    m_receiverStarting = false;
    m_receiverPanel->setStats(stats);
    if (stats.listening == m_receiverListening && !m_receiverAction->text().isEmpty()) {
        return;
    }
    m_receiverListening = stats.listening;
    if (stats.listening) {
        m_receiverAction->setText(tr("Stop &Receiver"));
        m_receiverAction->setIcon(Theme::glyphIcon(Theme::Glyph::Stop));
        m_receiverAction->setToolTip(tr("Stop receiving logs"));
    } else {
        m_receiverAction->setText(tr("Start &Receiver"));
        m_receiverAction->setIcon(Theme::glyphIcon(Theme::Glyph::Start));
        m_receiverAction->setToolTip(tr("Receive logs over TCP on %1:%2")
                                         .arg(QHostAddress(kReceiverAddress).toString())
                                         .arg(kReceiverPort));
    }
}

void MainWindow::showReceiverError(const QString& message) {
    // A start that fails almost always means that another program holds the port.
    reportError(
        m_receiverStarting
            ? tr("%1\n\nAnother program, or another LogVault window, may be using this port.").arg(message)
            : message);
}

void MainWindow::chooseImportFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Import Files"), QString(), tr("Log files (*.log *.txt *.json *.jsonl);;All files (*)"));
    if (!paths.isEmpty()) {
        importFiles(paths);
    }
}

void MainWindow::importFiles(const QStringList& paths) {
    if (!canImport()) {
        return;
    }
    QStringList sourceNames;
    for (const SourceInfo& source : m_sourcesPanel->sources()) {
        sourceNames.append(source.name);
    }
    ImportDialog dialog(paths, sourceNames, this);
    // The database may have changed while the dialog was open.
    if (dialog.exec() != QDialog::Accepted || !canImport() ||
        !m_importer->start(m_databasePath, dialog.files())) {
        return;
    }
    m_importTask->start(tr("Importing..."), true, true);
    updateActions();
}

void MainWindow::showImportProgress(int percent, const QString& fileName) {
    m_importTask->setText(tr("Importing %1").arg(fileName));
    m_importTask->setProgress(percent);
}

void MainWindow::showImportSummary(const ImportSummary& summary) {
    m_importTask->finish();
    updateActions();
    search();

    const QString counts = tr("Added %1 records, %2 lines not parsed, took %3.")
                               .arg(Format::count(summary.added), Format::count(summary.unparsed),
                                    Format::duration(summary.elapsedMs));
    if (!summary.error.isEmpty()) {
        QMessageBox::warning(this, tr("Import"),
                             tr("The import stopped: %1\n\n%2").arg(summary.error, counts));
    } else if (summary.cancelled) {
        QMessageBox::information(
            this, tr("Import"),
            tr("The import was cancelled; the records added before stay.\n\n%1").arg(counts));
    } else {
        QMessageBox::information(this, tr("Import"), counts);
    }
}

bool MainWindow::canImport() const { return !m_databasePath.isEmpty() && !m_importer->isRunning(); }

void MainWindow::renameSource() {
    const std::optional<SourceInfo> source = m_sourcesPanel->selectedSource();
    if (!source || m_jobs->isRunning()) {
        return;
    }
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename Source"), tr("New name of \"%1\":").arg(source->name),
                              QLineEdit::Normal, source->name, &accepted);
    if (!accepted || name.trimmed() == source->name) {
        return;
    }
    if (m_jobs->renameSource(m_databasePath, source->id, name)) {
        m_jobSource = source->name;
        showJob(tr("Renaming \"%1\"...").arg(source->name), false);
    }
}

void MainWindow::deleteSource() {
    const std::optional<SourceInfo> source = m_sourcesPanel->selectedSource();
    if (!source || m_jobs->isRunning()) {
        return;
    }
    QMessageBox question(QMessageBox::Warning, tr("Delete Source"),
                         tr("Delete the source \"%1\" and its %2 records?\n\nThis cannot be undone.")
                             .arg(source->name, Format::count(source->recordCount)),
                         QMessageBox::Cancel, this);
    const QAbstractButton* remove = question.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    question.setDefaultButton(QMessageBox::Cancel);
    question.exec();
    if (question.clickedButton() != remove) {
        return;
    }
    if (m_jobs->deleteSource(m_databasePath, source->id)) {
        m_jobSource = source->name;
        showJob(tr("Deleting \"%1\"...").arg(source->name), true);
    }
}

void MainWindow::deleteOldRecords() {
    if (m_databasePath.isEmpty() || m_jobs->isRunning()) {
        return;
    }
    const std::optional<SourceInfo> selected = m_sourcesPanel->selectedSource();
    DeleteRecordsDialog dialog(m_sourcesPanel->sources(),
                               selected ? std::optional<qint64>(selected->id) : std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString when = Format::dateTime(dialog.before());
    const QString what =
        dialog.sourceId() == 0
            ? tr("Delete the records of all sources older than %1?").arg(when)
            : tr("Delete the records of \"%1\" older than %2?").arg(dialog.sourceName(), when);
    QMessageBox question(QMessageBox::Warning, tr("Delete Records Older Than"), what, QMessageBox::Cancel,
                         this);
    question.setInformativeText(tr("This cannot be undone."));
    const QAbstractButton* remove = question.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    question.setDefaultButton(QMessageBox::Cancel);
    question.exec();
    // The database may have been closed while the dialogs were open.
    if (question.clickedButton() != remove || m_databasePath.isEmpty()) {
        return;
    }
    if (m_jobs->deleteRecordsOlderThan(m_databasePath, dialog.before(), dialog.sourceId())) {
        showJob(tr("Deleting records older than %1...").arg(when), true);
    }
}

void MainWindow::chooseExportFile() {
    if (m_databasePath.isEmpty() || m_exporter->isRunning()) {
        return;
    }
    const QString csvFilter = tr("CSV files (*.csv)");
    const QString jsonFilter = tr("JSON Lines files (*.jsonl)");
    QString filter = csvFilter;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Results"),
        dialogDirectory() + u"/"_s + QFileInfo(m_databasePath).completeBaseName() + u"-export.csv"_s,
        csvFilter + u";;"_s + jsonFilter, &filter);
    if (path.isEmpty() || m_databasePath.isEmpty()) {
        return;
    }
    // The extension decides the format; a name without one gets the extension of the chosen filter.
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) {
        suffix = filter == jsonFilter ? u"jsonl"_s : u"csv"_s;
        path += u'.' + suffix;
    }
    const ExportFormat format =
        suffix == u"jsonl"_s || suffix == u"json"_s ? ExportFormat::JsonLines : ExportFormat::Csv;
    // The table shows at most maxRows; the export takes every match of the same filters.
    if (m_exporter->start(m_databasePath, currentFilters(), path, format, Exporter::listSeparator())) {
        m_exportTask->start(tr("Exporting to %1").arg(QFileInfo(path).fileName()), true, true);
        updateActions();
    }
}

void MainWindow::showExportSummary(const ExportSummary& summary) {
    m_exportTask->finish();
    updateActions();
    const QString nativePath = QDir::toNativeSeparators(summary.path);
    if (!summary.error.isEmpty()) {
        QMessageBox::warning(this, tr("Export Results"), tr("The export failed: %1").arg(summary.error));
    } else if (summary.cancelled) {
        statusBar()->showMessage(tr("The export was cancelled; %1 was not written.").arg(nativePath),
                                 kMessageTimeoutMs);
    } else {
        statusBar()->showMessage(
            tr("Exported %1 records to %2 in %3.")
                .arg(Format::count(summary.exported), nativePath, Format::duration(summary.elapsedMs)),
            kMessageTimeoutMs);
    }
}

void MainWindow::showJob(const QString& text, bool cancellable) {
    // Only the deletions can be cancelled, and only they report progress.
    m_jobTask->start(text, cancellable, cancellable);
    updateActions();
}

void MainWindow::showJobResult(const DatabaseJobResult& result) {
    m_jobTask->finish();
    updateActions();
    switch (result.kind) {
    case DatabaseJobResult::Kind::Open:
        if (result.error.isEmpty()) {
            switchDatabase(result.databasePath);
        } else {
            QMessageBox::warning(this, tr("Open Database"), result.error);
        }
        break;
    case DatabaseJobResult::Kind::RenameSource:
        search();
        if (!result.error.isEmpty()) {
            QMessageBox::warning(this, tr("Rename Source"), result.error);
        }
        break;
    case DatabaseJobResult::Kind::DeleteSource:
        search();
        if (!result.error.isEmpty()) {
            QMessageBox::warning(
                this, tr("Delete Source"),
                tr("%1\n\nRecords deleted before the error stay deleted.").arg(result.error));
        } else if (result.cancelled) {
            statusBar()->showMessage(tr("Deleting \"%1\" was cancelled; %2 of its records were deleted.")
                                         .arg(m_jobSource, Format::count(result.deleted)),
                                     kMessageTimeoutMs);
        } else {
            statusBar()->showMessage(tr("Deleted the source \"%1\" and its %2 records.")
                                         .arg(m_jobSource, Format::count(result.deleted)),
                                     kMessageTimeoutMs);
        }
        break;
    case DatabaseJobResult::Kind::DeleteOldRecords:
        search();
        if (!result.error.isEmpty()) {
            QMessageBox::warning(
                this, tr("Delete Old Records"),
                tr("%1\n\nRecords deleted before the error stay deleted.").arg(result.error));
        } else if (result.cancelled) {
            statusBar()->showMessage(tr("Deleting old records was cancelled; %1 records were deleted.")
                                         .arg(Format::count(result.deleted)),
                                     kMessageTimeoutMs);
        } else {
            statusBar()->showMessage(tr("Deleted %1 old records.").arg(Format::count(result.deleted)),
                                     kMessageTimeoutMs);
        }
        break;
    }
}

SearchRequest MainWindow::currentFilters() const {
    SearchRequest request;
    m_filterBar->fillRequest(request, QDateTime::currentMSecsSinceEpoch());
    request.sourceIds = m_sourcesPanel->selectedIds();
    return request;
}

void MainWindow::search() {
    if (m_databasePath.isEmpty()) {
        return;
    }
    SearchRequest request = currentFilters();
    request.maxRows = kMaxRows;
    m_statsView->prepareRequest(request, m_tabs->currentWidget() == m_statsView);
    // SearchService delivers the result of the latest search only, so an earlier one never overwrites it.
    m_searchService->search(request);
    m_live->searchStarted();
    m_searchingTimer->start();
}

void MainWindow::showSearchResult(const SearchResult& result) {
    m_searchingTimer->stop();
    m_live->searchFinished();
    if (!result.error.isEmpty()) {
        m_searchStatus->setText(tr("Search failed: %1").arg(result.error));
        return;
    }
    m_records->setRecords(result.rows);
    m_table->scrollToTop();
    m_histogram->setHistogram(result.histogram);
    m_statsView->showResult(result);
    m_sourcesPanel->setSources(result.sources);
    m_tabs->setTabText(m_tabs->indexOf(m_table), tr("Records (%1)").arg(Format::count(result.rows.size())));

    qint64 stored = 0;
    for (const SourceInfo& source : result.sources) {
        stored += source.recordCount;
    }
    const QString separator = u' ' + kMiddleDot + u' ';
    QString text = tr("Found %1 of %2").arg(Format::count(result.total), Format::count(stored));
    if (result.rows.size() < result.total) {
        text += separator + tr("showing the newest %1").arg(Format::count(result.rows.size()));
    }
    m_searchStatus->setText(text + separator + Format::duration(result.elapsedMs));
}

void MainWindow::showSelectedRecord() {
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        m_detailsPanel->clear();
        return;
    }
    // With several rows selected, the one the user clicked last is shown.
    const QModelIndex current = m_table->currentIndex();
    const bool currentSelected = current.isValid() && m_table->selectionModel()->isRowSelected(current.row());
    m_detailsPanel->showRecord(m_records->record(currentSelected ? current.row() : rows.last().row()));
}

void MainWindow::copySelectedRawLines() {
    QList<int> rows;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    if (!rows.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_records->rawLines(rows));
    }
}

void MainWindow::showColumnMenu(const QPoint& position) {
    QHeaderView* header = m_table->horizontalHeader();
    QMenu menu(this);
    for (int column = 0; column < RecordTableModel::ColumnCount; ++column) {
        QAction* action = menu.addAction(m_records->headerData(column, Qt::Horizontal).toString());
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(column));
        // The last visible column stays: a table without columns has no header to bring them back.
        action->setEnabled(header->isSectionHidden(column) ||
                           header->count() - header->hiddenSectionCount() > 1);
        connect(action, &QAction::toggled, header,
                [header, column](bool visible) { header->setSectionHidden(column, !visible); });
    }
    menu.exec(header->viewport()->mapToGlobal(position));
}

void MainWindow::updateLivePause() {
    // The table must not move under the user: reading older records or a selected one pauses live mode.
    m_live->setPaused(m_table->verticalScrollBar()->value() > 0 || m_table->selectionModel()->hasSelection());
}

void MainWindow::updateLiveIndicator() {
    m_liveStatus->setVisible(m_live->isEnabled() && !m_databasePath.isEmpty());
    if (m_live->isPaused()) {
        m_liveStatus->setText(coloredDot(Theme::inactiveColor()) + u' ' + tr("Live paused").toHtmlEscaped() +
                              u" <a href=\"resume\">"_s + tr("Resume").toHtmlEscaped() + u"</a>"_s);
        m_liveStatus->setToolTip(
            tr("Live mode waits while the table is scrolled down or a record is selected."));
    } else {
        m_liveStatus->setText(coloredDot(Theme::activeColor()) + u' ' + tr("Live").toHtmlEscaped());
        m_liveStatus->setToolTip(tr("New records appear as they arrive, at most once per second."));
    }
}

void MainWindow::resumeLive() {
    m_table->clearSelection();
    m_table->scrollToTop();
}

void MainWindow::updateActions() {
    const bool open = !m_databasePath.isEmpty();
    const bool jobRunning = m_jobs->isRunning();
    const bool sourceSelected = open && m_sourcesPanel->selectedSource().has_value();
    m_newAction->setEnabled(!jobRunning);
    m_openAction->setEnabled(!jobRunning);
    m_importAction->setEnabled(canImport());
    m_renameSourceAction->setEnabled(sourceSelected && !jobRunning);
    m_deleteSourceAction->setEnabled(sourceSelected && !jobRunning);
    m_deleteOldAction->setEnabled(open && !jobRunning);
    m_exportAction->setEnabled(open && !m_exporter->isRunning());
    m_receiverAction->setEnabled(open);
    m_liveAction->setEnabled(open);
    m_refreshAction->setEnabled(open);
    m_findAction->setEnabled(open);
}

void MainWindow::reportError(const QString& message) {
    // Errors that come by themselves (intake, receiver) share one window, so a burst of them does not open
    // a window each.
    if (m_errorBox == nullptr) {
        m_errorBox =
            new QMessageBox(QMessageBox::Warning, AppInfo::name(), QString(), QMessageBox::Close, this);
        m_errorBox->setModal(false);
    }
    if (m_errorBox->isVisible()) {
        ++m_errorsSinceShown;
        m_errorBox->setInformativeText(tr("%1 more errors since then; the last one: %2")
                                           .arg(Format::count(m_errorsSinceShown), message));
        return;
    }
    m_errorsSinceShown = 0;
    m_errorBox->setText(message);
    m_errorBox->setInformativeText(QString());
    m_errorBox->show();
}

QString MainWindow::dialogDirectory() const {
    return m_databasePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                    : QFileInfo(m_databasePath).absolutePath();
}

QString MainWindow::databaseFilter() const { return tr("LogVault databases (*.lvdb);;All files (*)"); }
