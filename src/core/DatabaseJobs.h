#pragma once

#include "LogStore.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>

/// The outcome of a DatabaseJobs job.
struct DatabaseJobResult {
    enum class Kind { Open, RenameSource, DeleteSource, DeleteOldRecords };

    Kind kind = Kind::Open;
    QString databasePath;
    qint64 deleted = 0;     // deletions: the records deleted
    bool cancelled = false; // deletions: stopped by cancel(); the rest (and the source) stays
    QString error;          // empty on success
};

/// Runs short jobs on a database in the thread pool, each with a connection of its own (DESIGN.md sections
/// 3.3 and 6.7): opening a database file (creating it if it is new), renaming a source, deleting a source
/// with its records and deleting old records. One job runs at a time. Use the object from one thread;
/// finished() comes in that thread.
class DatabaseJobs : public QObject {
    Q_OBJECT

public:
    explicit DatabaseJobs(QObject* parent = nullptr);
    /// Cancels a running job and waits for it to end.
    ~DatabaseJobs() override;

    /// Each of these starts a job and returns true, or returns false while another job is running.
    bool open(const QString& databasePath);
    bool renameSource(const QString& databasePath, qint64 sourceId, const QString& name);
    bool deleteSource(const QString& databasePath, qint64 sourceId);
    /// Deletes the records older than before (ms UTC) of every source, or of one if sourceId is not 0.
    bool deleteRecordsOlderThan(const QString& databasePath, qint64 before, qint64 sourceId);

    /// Asks a running deletion to stop after the current chunk of records. Thread-safe.
    void cancel();

    bool isRunning() const;

    /// Blocks until the running job ends (when the database is closed).
    void waitForFinished();

signals:
    /// How much of a deletion is done, in percent: before the first chunk and after each one. Emitted from
    /// the pool thread.
    void progressChanged(int percent);
    void finished(const DatabaseJobResult& result);

private:
    using Work = std::function<void(LogStore& store, DatabaseJobResult& result)>;

    bool start(DatabaseJobResult::Kind kind, const QString& databasePath, Work work);
    LogStore::DeleteProgress progressReporter();
    void storeDeletion(const std::optional<qint64>& deleted, const LogStore& store,
                       DatabaseJobResult& result) const;

    QFutureWatcher<DatabaseJobResult> m_watcher;
    std::atomic<bool> m_cancel = false;
    bool m_running = false;
};
