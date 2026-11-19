#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

/// A file to import and the source its records go to.
struct ImportFile {
    QString path;
    QString source; // blank: the file name without its extension
};

/// The outcome of an import.
struct ImportSummary {
    qint64 added = 0;    // records written
    qint64 unparsed = 0; // of those, lines that were not recognized (stored with "_parse_error")
    qint64 elapsedMs = 0;
    bool cancelled = false; // records committed before the cancel stay
    QString error;          // why the import stopped; empty on success. Lines read before it are written.
};

/// Imports log files in the thread pool with its own database connection (DESIGN.md section 5.3): lines are
/// split like TCP lines, indented lines join the previous record (stack traces), and records are written in
/// transactions of kBatchSize. Use the object from one thread; progressChanged() comes from the pool thread.
class FileImporter : public QObject {
    Q_OBJECT

public:
    static constexpr int kBatchSize = 1000;
    static constexpr int kProgressIntervalMs = 100;

    explicit FileImporter(QObject* parent = nullptr);
    /// Cancels a running import and waits for it to end.
    ~FileImporter() override;

    /// Starts importing files into the database at databasePath. Returns false if an import is running.
    bool start(const QString& databasePath, const QVector<ImportFile>& files);

    /// Asks the running import to stop before its next transaction. Committed records stay. Thread-safe.
    void cancel();

    bool isRunning() const;

    /// Blocks until the running import ends (at shutdown, after cancel()).
    void waitForFinished();

signals:
    /// Percent of the bytes of all files read so far and the file being read; at most every
    /// kProgressIntervalMs. Emitted from the pool thread.
    void progressChanged(int percent, const QString& fileName);
    void finished(const ImportSummary& summary);

private:
    QFutureWatcher<ImportSummary> m_watcher;
    std::atomic<bool> m_cancel = false;
    bool m_running = false;
};
