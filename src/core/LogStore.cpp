#include "LogStore.h"

#include "Database.h"

#include <QDateTime>
#include <QSqlError>

#include <algorithm>

namespace {

// Qt binds a null QString as SQL NULL, which the NOT NULL text columns reject; an empty text is valid input.
QVariant nonNullText(const QString& text) {
    return text.isNull() ? QVariant(QStringLiteral("")) : QVariant(text);
}

// "WHERE <condition>" for queries over "records r", or nothing when every record matches.
QString whereClause(const SqlFragment& condition) {
    return condition.sql.isEmpty() ? QString() : QStringLiteral("WHERE ") + condition.sql;
}

// Reads the columns r.id, r.ts, r.level, s.name, r.message, r.fields, r.raw of the current row.
Record readRecord(const QSqlQuery& query) {
    Record record;
    record.id = query.value(0).toLongLong();
    record.ts = query.value(1).toLongLong();
    record.level = levelFromValue(query.value(2).toInt());
    record.source = query.value(3).toString();
    record.message = query.value(4).toString();
    record.fields = query.value(5).toString();
    record.raw = query.value(6).toString();
    return record;
}

// Both conditions; either may be empty.
SqlFragment bothOf(const SqlFragment& first, const SqlFragment& second) {
    if (first.sql.isEmpty() || second.sql.isEmpty()) {
        return first.sql.isEmpty() ? second : first;
    }
    return {first.sql + QStringLiteral(" AND ") + second.sql, first.values + second.values};
}

// The statistics query of DESIGN.md section 6.3 for a level, source or message mode. Each returns the
// columns id, value, count, largest groups first; ties are ordered by value so the order is stable.
SqlFragment statisticsQuery(const SqlFragment& condition, StatsMode mode) {
    const QString where = whereClause(condition);
    switch (mode) {
    case StatsMode::Level:
        return {QStringLiteral("SELECT r.level, NULL, count(*) FROM records r %1 "
                               "GROUP BY r.level ORDER BY 3 DESC, 1")
                    .arg(where),
                condition.values};
    case StatsMode::Source:
        return {QStringLiteral(
                    "SELECT s.id, s.name, count(*) FROM records r JOIN sources s ON s.id = r.source_id "
                    "%1 GROUP BY s.id ORDER BY 3 DESC, 2")
                    .arg(where),
                condition.values};
    case StatsMode::Message:
        return {QStringLiteral("SELECT 0, r.msg_key, count(*) FROM records r %1 "
                               "GROUP BY r.msg_key ORDER BY 3 DESC, 2 LIMIT ?")
                    .arg(where),
                condition.values + QVariantList{LogStore::kStatsRowLimit}};
    case StatsMode::None:
    case StatsMode::Field:
        break;
    }
    return {};
}

} // namespace

LogStore::LogStore(Database& database)
    : m_db(database.connection()), m_insertSource(m_db), m_selectSource(m_db), m_insertRecord(m_db) {
    Q_ASSERT(database.isOpen());
}

bool LogStore::insertBatch(const QVector<Record>& records) {
    if (records.isEmpty()) {
        return true;
    }
    if (writeBatch(records)) {
        return true;
    }
    return failWith(tr("Cannot write %1 records").arg(records.size()));
}

std::optional<QVector<SourceInfo>> LogStore::sources() {
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    // A count per source uses idx_records_source_ts instead of grouping the whole records table.
    const QString sql =
        QStringLiteral("SELECT s.id, s.name, (SELECT count(*) FROM records r WHERE r.source_id = s.id) "
                       "FROM sources s ORDER BY s.name COLLATE NOCASE, s.id");
    if (!execute(query, sql)) {
        failWith(tr("Cannot read the sources"));
        return std::nullopt;
    }
    QVector<SourceInfo> result;
    while (query.next()) {
        result.append({query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong()});
    }
    if (query.lastError().isValid()) {
        setError(query);
        failWith(tr("Cannot read the sources"));
        return std::nullopt;
    }
    return result;
}

bool LogStore::renameSource(qint64 id, const QString& name) {
    if (changeSourceName(id, name.trimmed())) {
        return true;
    }
    return failWith(tr("Cannot rename the source"));
}

std::optional<qint64> LogStore::deleteSource(qint64 id, const std::atomic<bool>* cancel) {
    const std::optional<qint64> deleted = deleteInChunks(QStringLiteral("source_id = ?"), {id}, cancel);
    if (!deleted) {
        failWith(tr("Cannot delete the source"));
        return std::nullopt;
    }
    if (cancel != nullptr && cancel->load()) {
        return deleted;
    }
    // Records another connection wrote after the last chunk go with the source through ON DELETE CASCADE.
    QSqlQuery query(m_db);
    if (!execute(query, QStringLiteral("DELETE FROM sources WHERE id = ?"), {id})) {
        failWith(tr("Cannot delete the source"));
        return std::nullopt;
    }
    return deleted;
}

std::optional<qint64> LogStore::deleteRecordsOlderThan(qint64 ts, qint64 sourceId,
                                                       const std::atomic<bool>* cancel) {
    const std::optional<qint64> deleted =
        sourceId == 0 ? deleteInChunks(QStringLiteral("ts < ?"), {ts}, cancel)
                      : deleteInChunks(QStringLiteral("source_id = ? AND ts < ?"), {sourceId, ts}, cancel);
    if (!deleted) {
        failWith(tr("Cannot delete old records"));
    }
    return deleted;
}

std::optional<qint64> LogStore::recordCount() {
    const std::optional<QVariant> count = selectValue(QStringLiteral("SELECT count(*) FROM records"));
    if (!count) {
        failWith(tr("Cannot count records"));
        return std::nullopt;
    }
    return count->toLongLong();
}

bool LogStore::beginRead() {
    // A deferred transaction takes no lock; the first query fixes the state that all of them see.
    QSqlQuery query(m_db);
    if (execute(query, QStringLiteral("BEGIN"))) {
        return true;
    }
    return failWith(tr("Cannot start reading"));
}

void LogStore::endRead() {
    QSqlQuery query(m_db);
    // Nothing was written, so a failure loses no data; it would mean a query is still active.
    if (!query.exec(QStringLiteral("COMMIT"))) {
        qWarning("LogStore: ending a read transaction failed: %s", qPrintable(query.lastError().text()));
        rollback();
    }
}

std::optional<QVector<Record>> LogStore::findRecords(const SqlFragment& condition, int limit) {
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    const QString sql = QStringLiteral("SELECT r.id, r.ts, r.level, s.name, r.message, r.fields, r.raw "
                                       "FROM records r JOIN sources s ON s.id = r.source_id %1 "
                                       "ORDER BY r.ts DESC, r.id DESC LIMIT ?")
                            .arg(whereClause(condition));
    // A negative LIMIT means "no limit" in SQLite.
    if (!execute(query, sql, condition.values + QVariantList{qMax(0, limit)})) {
        failWith(tr("Cannot read records"));
        return std::nullopt;
    }
    QVector<Record> result;
    while (query.next()) {
        result.append(readRecord(query));
    }
    if (query.lastError().isValid()) {
        setError(query);
        failWith(tr("Cannot read records"));
        return std::nullopt;
    }
    return result;
}

std::optional<MatchSpan> LogStore::matchSpan(const SqlFragment& condition) {
    // Separate min() and max() queries: SQLite answers each by walking the time index from one end and
    // stopping at the first match, so the two walks never overlap. count() would read every match.
    const QString where = whereClause(condition);
    const std::optional<QVariant> first =
        selectValue(QStringLiteral("SELECT min(r.ts) FROM records r %1").arg(where), condition.values);
    if (!first) {
        failWith(tr("Cannot search records"));
        return std::nullopt;
    }
    MatchSpan span;
    if (first->isNull()) {
        return span;
    }
    const std::optional<QVariant> last =
        selectValue(QStringLiteral("SELECT max(r.ts) FROM records r %1").arg(where), condition.values);
    if (!last) {
        failWith(tr("Cannot search records"));
        return std::nullopt;
    }
    span.found = true;
    span.firstTs = first->toLongLong();
    span.lastTs = last->toLongLong();
    return span;
}

std::optional<Histogram> LogStore::histogram(const SqlFragment& condition, qint64 first, qint64 last) {
    Histogram histogram = Histogram::forRange(first, last);
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    const QString sql =
        QStringLiteral("SELECT (r.ts - ?) / ?, r.level, count(*) FROM records r %1 GROUP BY 1, 2")
            .arg(whereClause(condition));
    if (!execute(query, sql, QVariantList{histogram.start, histogram.step} + condition.values)) {
        failWith(tr("Cannot build the histogram"));
        return std::nullopt;
    }
    while (query.next()) {
        const qint64 bar = query.value(0).toLongLong();
        const Level level = levelFromValue(query.value(1).toInt());
        if (bar >= 0 && bar < histogram.bars.size()) {
            histogram.bars[bar][static_cast<int>(level)] += query.value(2).toLongLong();
        }
    }
    if (query.lastError().isValid()) {
        setError(query);
        failWith(tr("Cannot build the histogram"));
        return std::nullopt;
    }
    return histogram;
}

std::optional<QVector<StatsRow>> LogStore::statistics(const SqlFragment& condition, StatsMode mode) {
    if (mode != StatsMode::Level && mode != StatsMode::Source && mode != StatsMode::Message) {
        return QVector<StatsRow>();
    }
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    const SqlFragment select = statisticsQuery(condition, mode);
    if (!execute(query, select.sql, select.values)) {
        failWith(tr("Cannot compute statistics"));
        return std::nullopt;
    }
    QVector<StatsRow> rows;
    while (query.next()) {
        StatsRow row;
        row.id = query.value(0).toLongLong();
        row.count = query.value(2).toLongLong();
        row.value = mode == StatsMode::Level ? levelName(levelFromValue(static_cast<int>(row.id)))
                                             : query.value(1).toString();
        rows.append(row);
    }
    if (query.lastError().isValid()) {
        setError(query);
        failWith(tr("Cannot compute statistics"));
        return std::nullopt;
    }
    return rows;
}

std::optional<QVector<StatsRow>> LogStore::fieldStatistics(const SqlFragment& condition, const QString& field,
                                                           qint64 matchCount) {
    if (!SearchQuery::isValidFieldName(field)) {
        m_errorString = tr("\"%1\" is not a valid field name").arg(field);
        failWith(tr("Cannot compute statistics"));
        return std::nullopt;
    }
    // Only records that mention the field are grouped: sorting every match just to count the ones without
    // the field would take most of the time. The window sum counts the records with the field in all
    // groups, not only in the first kStatsRowLimit.
    const SqlFragment value = SearchQuery::fieldValue(field);
    const SqlFragment where = bothOf(condition, SearchQuery::fieldMention(field));
    const QString sql =
        QStringLiteral("SELECT %1 AS field_value, count(*), sum(count(*)) OVER () FROM records r WHERE %2 "
                       "GROUP BY 1 HAVING field_value IS NOT NULL ORDER BY 2 DESC, 1 LIMIT ?")
            .arg(value.sql, where.sql);
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if (!execute(query, sql, value.values + where.values + QVariantList{kStatsRowLimit})) {
        failWith(tr("Cannot compute statistics"));
        return std::nullopt;
    }
    QVector<StatsRow> rows;
    qint64 withField = 0;
    while (query.next()) {
        StatsRow row;
        row.value = query.value(0).toString();
        row.count = query.value(1).toLongLong();
        rows.append(row);
        withField = query.value(2).toLongLong();
    }
    if (query.lastError().isValid()) {
        setError(query);
        failWith(tr("Cannot compute statistics"));
        return std::nullopt;
    }
    // The matches without the field (or with null in it) form one group, placed by its size; it goes before
    // groups of the same size, where SQL puts NULL.
    const qint64 withoutField = matchCount - withField;
    if (withoutField > 0) {
        StatsRow row;
        row.value = tr("(no field)");
        row.count = withoutField;
        row.noField = true;
        const auto position = std::find_if(rows.begin(), rows.end(), [withoutField](const StatsRow& other) {
            return other.count <= withoutField;
        });
        rows.insert(position, row);
        if (rows.size() > kStatsRowLimit) {
            rows.removeLast();
        }
    }
    return rows;
}

QString LogStore::errorString() const { return m_errorString; }

bool LogStore::prepareInsertQueries() {
    if (m_insertQueriesPrepared) {
        return true;
    }
    if (!m_insertSource.prepare(
            QStringLiteral("INSERT OR IGNORE INTO sources (name, created_at) VALUES (?, ?)"))) {
        return setError(m_insertSource);
    }
    m_selectSource.setForwardOnly(true);
    if (!m_selectSource.prepare(QStringLiteral("SELECT id FROM sources WHERE name = ?"))) {
        return setError(m_selectSource);
    }
    if (!m_insertRecord.prepare(
            QStringLiteral("INSERT INTO records (ts, level, source_id, message, msg_key, fields, raw) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?)"))) {
        return setError(m_insertRecord);
    }
    m_insertQueriesPrepared = true;
    return true;
}

bool LogStore::writeBatch(const QVector<Record>& records) {
    if (!prepareInsertQueries() || !beginWrite()) {
        return false;
    }
    // Source ids are cached for this transaction only: between batches another connection may rename or
    // delete a source, and a stale id would put records under the wrong source.
    QHash<QString, qint64> sourceIds;
    for (const Record& record : records) {
        const std::optional<qint64> sourceId = sourceIdFor(record.source, sourceIds);
        if (!sourceId) {
            rollback();
            return false;
        }
        m_insertRecord.bindValue(0, record.ts);
        m_insertRecord.bindValue(1, static_cast<int>(record.level));
        m_insertRecord.bindValue(2, *sourceId);
        m_insertRecord.bindValue(3, nonNullText(record.message));
        m_insertRecord.bindValue(4, nonNullText(messageKey(record.message)));
        m_insertRecord.bindValue(5, record.fields.isEmpty() ? QVariant() : QVariant(record.fields));
        m_insertRecord.bindValue(6, nonNullText(record.raw));
        if (!m_insertRecord.exec()) {
            setError(m_insertRecord);
            rollback();
            return false;
        }
    }
    return commit();
}

std::optional<qint64> LogStore::sourceIdFor(const QString& name, QHash<QString, qint64>& cache) {
    if (const auto it = cache.constFind(name); it != cache.constEnd()) {
        return *it;
    }
    m_insertSource.bindValue(0, nonNullText(name));
    m_insertSource.bindValue(1, QDateTime::currentMSecsSinceEpoch());
    if (!m_insertSource.exec()) {
        setError(m_insertSource);
        return std::nullopt;
    }
    m_selectSource.bindValue(0, nonNullText(name));
    if (!m_selectSource.exec()) {
        setError(m_selectSource);
        return std::nullopt;
    }
    if (!m_selectSource.next()) {
        if (m_selectSource.lastError().isValid()) {
            setError(m_selectSource);
        } else {
            m_errorString = tr("source \"%1\" was not created").arg(name);
        }
        return std::nullopt;
    }
    const qint64 id = m_selectSource.value(0).toLongLong();
    m_selectSource.finish();
    cache.insert(name, id);
    return id;
}

bool LogStore::changeSourceName(qint64 id, const QString& name) {
    if (name.isEmpty()) {
        m_errorString = tr("the name is empty");
        return false;
    }
    if (!beginWrite()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if (!execute(query, QStringLiteral("SELECT id FROM sources WHERE name = ?"), {name})) {
        rollback();
        return false;
    }
    const bool taken = query.next() && query.value(0).toLongLong() != id;
    if (query.lastError().isValid()) {
        setError(query);
        rollback();
        return false;
    }
    query.finish();
    if (taken) {
        m_errorString = tr("a source named \"%1\" already exists").arg(name);
        rollback();
        return false;
    }
    if (!execute(query, QStringLiteral("UPDATE sources SET name = ? WHERE id = ?"), {name, id})) {
        rollback();
        return false;
    }
    if (query.numRowsAffected() == 0) {
        m_errorString = tr("the source does not exist");
        rollback();
        return false;
    }
    return commit();
}

std::optional<qint64> LogStore::deleteInChunks(const QString& condition, const QVariantList& values,
                                               const std::atomic<bool>* cancel) {
    // The condition is a fixed fragment written in this class; every value is bound.
    const QString sql =
        QStringLiteral("DELETE FROM records WHERE id IN (SELECT id FROM records WHERE %1 LIMIT ?)")
            .arg(condition);
    QVariantList chunkValues = values;
    chunkValues.append(kDeleteChunkSize);

    QSqlQuery query(m_db);
    qint64 deleted = 0;
    while (cancel == nullptr || !cancel->load()) {
        // Each chunk is its own transaction, so intake and import wait for the lock only briefly.
        if (!beginWrite()) {
            return std::nullopt;
        }
        if (!execute(query, sql, chunkValues)) {
            rollback();
            return std::nullopt;
        }
        const int chunk = query.numRowsAffected();
        if (!commit()) {
            return std::nullopt;
        }
        deleted += chunk;
        if (chunk < kDeleteChunkSize) {
            break;
        }
    }
    return deleted;
}

bool LogStore::beginWrite() {
    // IMMEDIATE takes the write lock now, waiting up to busy_timeout. A deferred transaction that reads first
    // and writes later can fail at once with "database is locked" when another connection writes meanwhile.
    QSqlQuery query(m_db);
    return execute(query, QStringLiteral("BEGIN IMMEDIATE"));
}

bool LogStore::commit() {
    QSqlQuery query(m_db);
    if (execute(query, QStringLiteral("COMMIT"))) {
        return true;
    }
    // A failed COMMIT (e.g. a busy lock) leaves the transaction open.
    rollback();
    return false;
}

void LogStore::rollback() {
    // The error that caused the rollback is already in m_errorString and must not be replaced.
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("ROLLBACK"))) {
        qWarning("LogStore: ROLLBACK failed: %s", qPrintable(query.lastError().text()));
    }
}

bool LogStore::execute(QSqlQuery& query, const QString& sql, const QVariantList& values) {
    if (!query.prepare(sql)) {
        return setError(query);
    }
    for (qsizetype i = 0; i < values.size(); ++i) {
        query.bindValue(static_cast<int>(i), values.at(i));
    }
    if (!query.exec()) {
        return setError(query);
    }
    return true;
}

std::optional<QVariant> LogStore::selectValue(const QString& sql, const QVariantList& values) {
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if (!execute(query, sql, values)) {
        return std::nullopt;
    }
    if (!query.next()) {
        setError(query);
        return std::nullopt;
    }
    return query.value(0);
}

bool LogStore::setError(const QSqlQuery& query) {
    m_errorString = query.lastError().text();
    return false;
}

bool LogStore::failWith(const QString& action) {
    m_errorString = tr("%1: %2").arg(action, m_errorString);
    return false;
}
