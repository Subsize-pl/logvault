#pragma once

#include "Record.h"

#include <QCoreApplication>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <atomic>
#include <optional>

class Database;

/// A source of records (a TCP client name or an imported file) and how many records it has.
struct SourceInfo {
    qint64 id = 0;
    QString name;
    qint64 recordCount = 0;
};

/// The only place with SQL: writes records, manages sources and deletes old data on one Database connection.
/// Use it only in the thread that opened the database, and destroy it before that Database.
/// Methods report failure through their return value and errorString().
class LogStore {
    Q_DECLARE_TR_FUNCTIONS(LogStore)

public:
    /// Records deleted per transaction, so other connections never wait long for the write lock.
    static constexpr int kDeleteChunkSize = 10'000;

    /// The database must be open.
    explicit LogStore(Database& database);

    LogStore(const LogStore&) = delete;
    LogStore& operator=(const LogStore&) = delete;

    /// Writes all records in one transaction, creating missing sources by name; Record::id is ignored.
    /// On failure nothing is written.
    bool insertBatch(const QVector<Record>& records);

    /// All sources, sorted by name.
    std::optional<QVector<SourceInfo>> sources();

    /// Fails if the name is empty, is taken by another source, or the source does not exist.
    /// Surrounding whitespace is removed.
    bool renameSource(qint64 id, const QString& name);

    /// Deletes the records of the source in chunks of kDeleteChunkSize, then the source itself, and returns
    /// the number of records deleted. When *cancel becomes true, stops after the current chunk and keeps the
    /// source. On error returns nullopt; chunks deleted before the error stay deleted.
    std::optional<qint64> deleteSource(qint64 id, const std::atomic<bool>* cancel = nullptr);

    /// Deletes records with a time strictly before ts (of one source if sourceId is not 0) in chunks, like
    /// deleteSource(), and returns their number. Sources are kept even when they become empty.
    std::optional<qint64> deleteRecordsOlderThan(qint64 ts, qint64 sourceId = 0,
                                                 const std::atomic<bool>* cancel = nullptr);

    std::optional<qint64> recordCount();

    /// The newest records (by time, then by insertion order), at most limit of them.
    std::optional<QVector<Record>> latestRecords(int limit);

    QString errorString() const;

private:
    bool prepareInsertQueries();
    bool writeBatch(const QVector<Record>& records);
    std::optional<qint64> sourceIdFor(const QString& name, QHash<QString, qint64>& cache);
    bool changeSourceName(qint64 id, const QString& name);
    std::optional<qint64> deleteInChunks(const QString& condition, const QVariantList& values,
                                         const std::atomic<bool>* cancel);

    bool beginWrite();
    bool commit();
    void rollback();
    bool execute(QSqlQuery& query, const QString& sql, const QVariantList& values = {});
    bool setError(const QSqlQuery& query);
    bool failWith(const QString& action);

    QSqlDatabase m_db;
    QSqlQuery m_insertSource;
    QSqlQuery m_selectSource;
    QSqlQuery m_insertRecord;
    bool m_insertQueriesPrepared = false;
    QString m_errorString;
};
