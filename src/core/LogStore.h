#pragma once

#include "Record.h"
#include "SearchQuery.h"

#include <QCoreApplication>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVector>

#include <atomic>
#include <functional>
#include <optional>

class Database;

/// A source of records (a TCP client name or an imported file) and how many records it has.
struct SourceInfo {
    qint64 id = 0;
    QString name;
    qint64 recordCount = 0;
};

/// Where in time the records that match a condition are.
struct MatchSpan {
    bool found = false; // false when nothing matches
    qint64 firstTs = 0; // ms UTC of the oldest match
    qint64 lastTs = 0;  // ms UTC of the newest match
};

/// The only place with SQL: writes, searches and deletes records and manages sources on one Database
/// connection. Use it only in the thread that opened the database, and destroy it before that Database.
/// Methods report failure through their return value and errorString().
class LogStore {
    Q_DECLARE_TR_FUNCTIONS(LogStore)

public:
    /// Records deleted per transaction, so other connections never wait long for the write lock.
    static constexpr int kDeleteChunkSize = 10'000;
    /// Statistics rows at most when grouping by message or by field.
    static constexpr int kStatsRowLimit = 100;

    /// Called before the first chunk of a deletion and after each one, with the records deleted so far and
    /// the number that matched when the deletion started.
    using DeleteProgress = std::function<void(qint64 deleted, qint64 total)>;

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
    /// source. On error returns nullopt; chunks deleted before the error stay deleted. With a progress
    /// callback, the records are counted first.
    std::optional<qint64> deleteSource(qint64 id, const std::atomic<bool>* cancel = nullptr,
                                       const DeleteProgress& progress = {});

    /// Deletes records with a time strictly before ts (of one source if sourceId is not 0) in chunks, like
    /// deleteSource(), and returns their number. Sources are kept even when they become empty.
    std::optional<qint64> deleteRecordsOlderThan(qint64 ts, qint64 sourceId = 0,
                                                 const std::atomic<bool>* cancel = nullptr,
                                                 const DeleteProgress& progress = {});

    std::optional<qint64> recordCount();

    /// The number of records matching a condition built by SearchQuery. It reads every match, so a search
    /// counts with the histogram instead (DESIGN.md section 6.3).
    std::optional<qint64> countRecords(const SqlFragment& condition);

    /// Starts a read transaction: the queries until endRead() see one state of the database, even while
    /// other connections write.
    bool beginRead();
    void endRead();

    /// The records matching a condition built by SearchQuery, newest first (by time, then by insertion
    /// order), at most limit of them.
    std::optional<QVector<Record>> findRecords(const SqlFragment& condition, int limit);

    /// Passes every record matching a condition to visit, in the order of findRecords(), until visit returns
    /// false. The rows are read one at a time, so any number of them can be visited. Returns false only when
    /// reading fails.
    bool visitRecords(const SqlFragment& condition, const std::function<bool(const Record&)>& visit);

    /// The times of the oldest and the newest match. Cheap unless matches are rare: each end is found by
    /// walking the time index from that side up to the first match.
    std::optional<MatchSpan> matchSpan(const SqlFragment& condition);

    /// Counts the matches per level in the bars of Histogram::forRange(first, last). first must not be later
    /// than the oldest match; matches after the last bar are not counted.
    std::optional<Histogram> histogram(const SqlFragment& condition, qint64 first, qint64 last);

    /// Groups the matches by level, source or message pattern (msg_key), largest groups first; at most
    /// kStatsRowLimit rows by message. Other modes give no rows (for fields, see fieldStatistics()).
    std::optional<QVector<StatsRow>> statistics(const SqlFragment& condition, StatsMode mode);

    /// Groups the matches by the value of a field, largest groups first, at most kStatsRowLimit rows; the
    /// matches without the field make one "(no field)" row. matchCount is the number of matches (the
    /// histogram total): only records that mention the field are grouped, and the rest are counted by
    /// subtraction. The name must be valid (SearchQuery::isValidFieldName()).
    std::optional<QVector<StatsRow>> fieldStatistics(const SqlFragment& condition, const QString& field,
                                                     qint64 matchCount);

    QString errorString() const;

private:
    bool prepareInsertQueries();
    bool writeBatch(const QVector<Record>& records);
    std::optional<qint64> sourceIdFor(const QString& name, QHash<QString, qint64>& cache);
    bool changeSourceName(qint64 id, const QString& name);
    std::optional<qint64> deleteInChunks(const QString& condition, const QVariantList& values,
                                         const std::atomic<bool>* cancel, const DeleteProgress& progress);

    bool beginWrite();
    bool commit();
    void rollback();
    bool execute(QSqlQuery& query, const QString& sql, const QVariantList& values = {});
    std::optional<QVariant> selectValue(const QString& sql, const QVariantList& values = {});
    bool setError(const QSqlQuery& query);
    bool failWith(const QString& action);

    QSqlDatabase m_db;
    QSqlQuery m_insertSource;
    QSqlQuery m_selectSource;
    QSqlQuery m_insertRecord;
    bool m_insertQueriesPrepared = false;
    QString m_errorString;
};
