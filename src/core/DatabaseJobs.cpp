#include "DatabaseJobs.h"

#include "Database.h"
#include "LogStore.h"

#include <QtConcurrentRun>

#include <optional>
#include <utility>

DatabaseJobs::DatabaseJobs(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<DatabaseJobResult>::finished, this, [this] {
        m_running = false;
        emit finished(m_watcher.result());
    });
}

DatabaseJobs::~DatabaseJobs() {
    // A job uses this object's cancel flag, so it must end before the object does.
    cancel();
    waitForFinished();
}

bool DatabaseJobs::open(const QString& databasePath) {
    // Opening is the whole job: it creates the schema of a new file and checks the schema of an old one.
    return start(DatabaseJobResult::Kind::Open, databasePath, [](LogStore&, DatabaseJobResult&) {});
}

bool DatabaseJobs::renameSource(const QString& databasePath, qint64 sourceId, const QString& name) {
    return start(DatabaseJobResult::Kind::RenameSource, databasePath,
                 [sourceId, name](LogStore& store, DatabaseJobResult& result) {
                     if (!store.renameSource(sourceId, name)) {
                         result.error = store.errorString();
                     }
                 });
}

bool DatabaseJobs::deleteSource(const QString& databasePath, qint64 sourceId) {
    return start(DatabaseJobResult::Kind::DeleteSource, databasePath,
                 [this, sourceId](LogStore& store, DatabaseJobResult& result) {
                     storeDeletion(store.deleteSource(sourceId, &m_cancel, progressReporter()), store,
                                   result);
                 });
}

bool DatabaseJobs::deleteRecordsOlderThan(const QString& databasePath, qint64 before, qint64 sourceId) {
    return start(DatabaseJobResult::Kind::DeleteOldRecords, databasePath,
                 [this, before, sourceId](LogStore& store, DatabaseJobResult& result) {
                     storeDeletion(
                         store.deleteRecordsOlderThan(before, sourceId, &m_cancel, progressReporter()), store,
                         result);
                 });
}

void DatabaseJobs::cancel() { m_cancel = true; }

bool DatabaseJobs::isRunning() const { return m_running; }

void DatabaseJobs::waitForFinished() { m_watcher.waitForFinished(); }

LogStore::DeleteProgress DatabaseJobs::progressReporter() {
    return [this](qint64 deleted, qint64 total) {
        // Records written while the deletion runs can match too, so the share is capped.
        emit progressChanged(total > 0 ? static_cast<int>(qMin<qint64>(deleted * 100 / total, 100)) : 100);
    };
}

void DatabaseJobs::storeDeletion(const std::optional<qint64>& deleted, const LogStore& store,
                                 DatabaseJobResult& result) const {
    if (!deleted) {
        result.error = store.errorString();
        return;
    }
    result.deleted = *deleted;
    // LogStore stops between chunks when the flag is set (and keeps the source then).
    result.cancelled = m_cancel.load();
}

bool DatabaseJobs::start(DatabaseJobResult::Kind kind, const QString& databasePath, Work work) {
    if (m_running) {
        return false;
    }
    m_running = true;
    m_cancel = false;
    DatabaseJobResult result;
    result.kind = kind;
    result.databasePath = databasePath;
    m_watcher.setFuture(QtConcurrent::run([result = std::move(result), work = std::move(work)]() mutable {
        // The connection belongs to this pool thread and is closed before the job returns.
        Database database;
        if (!database.open(result.databasePath)) {
            result.error = database.errorString();
            return result;
        }
        LogStore store(database);
        work(store, result);
        return result;
    }));
    return true;
}
