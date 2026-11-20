#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

class LogStore;

/// The outcome of a DatabaseJobs job.
struct DatabaseJobResult {
    enum class Kind { Open, RenameSource, DeleteSource };

    Kind kind = Kind::Open;
    QString databasePath;
    qint64 deleted = 0;     // DeleteSource: the records deleted
    bool cancelled = false; // DeleteSource: stopped by cancel(); the source and its other records stay
    QString error;          // empty on success
};

/// Runs short jobs on a database in the thread pool, each with a connection of its own (DESIGN.md sections
/// 3.3 and 6.7): opening a database file (creating it if it is new), renaming a source, and deleting a source
/// with its records. One job runs at a time. Use the object from one thread; finished() comes in that thread.
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

    /// Asks a running deletion to stop after the current chunk of records. Thread-safe.
    void cancel();

    bool isRunning() const;

    /// Blocks until the running job ends (when the database is closed).
    void waitForFinished();

signals:
    void finished(const DatabaseJobResult& result);

private:
    using Work = std::function<void(LogStore& store, DatabaseJobResult& result)>;

    bool start(DatabaseJobResult::Kind kind, const QString& databasePath, Work work);

    QFutureWatcher<DatabaseJobResult> m_watcher;
    std::atomic<bool> m_cancel = false;
    bool m_running = false;
};
