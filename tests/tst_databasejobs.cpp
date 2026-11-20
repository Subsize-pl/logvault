#include "tst_databasejobs.h"

#include "Database.h"
#include "DatabaseJobs.h"
#include "LogStore.h"

#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTest>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr int kWaitMs = 60'000;

// Waits for the job that was just started; nullopt if it did not start or did not end in time.
std::optional<DatabaseJobResult> waitForJob(QSignalSpy& finished, bool started) {
    if (!started || !finished.wait(kWaitMs)) {
        return std::nullopt;
    }
    return finished.takeFirst().first().value<DatabaseJobResult>();
}

// The sources of a database as name -> record count, read with a connection of the test thread.
std::optional<QHash<QString, qint64>> sourceCounts(const QString& path) {
    Database database;
    if (!database.open(path)) {
        return std::nullopt;
    }
    LogStore store(database);
    const std::optional<QVector<SourceInfo>> sources = store.sources();
    if (!sources) {
        return std::nullopt;
    }
    QHash<QString, qint64> counts;
    for (const SourceInfo& source : *sources) {
        counts.insert(source.name, source.recordCount);
    }
    return counts;
}

std::optional<qint64> sourceId(const QString& path, const QString& name) {
    Database database;
    if (!database.open(path)) {
        return std::nullopt;
    }
    LogStore store(database);
    const std::optional<QVector<SourceInfo>> sources = store.sources();
    for (const SourceInfo& source : sources.value_or(QVector<SourceInfo>())) {
        if (source.name == name) {
            return source.id;
        }
    }
    return std::nullopt;
}

} // namespace

void TestDatabaseJobs::init() {
    // A warning means something was not shut down cleanly (a pool task, a connection still in use).
    QTest::failOnWarning();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestDatabaseJobs::cleanup() { m_dir.reset(); }

QString TestDatabaseJobs::databasePath() const { return m_dir->filePath(u"jobs.lvdb"_s); }

bool TestDatabaseJobs::fill(const QStringList& sources, int count) const {
    Database database;
    if (!database.open(databasePath())) {
        return false;
    }
    LogStore store(database);
    QVector<Record> records;
    for (const QString& source : sources) {
        for (int i = 0; i < count; ++i) {
            Record record;
            record.ts = 1'000 + i;
            record.level = Level::Info;
            record.source = source;
            record.message = u"record %1"_s.arg(i);
            record.raw = record.message;
            records.append(record);
        }
    }
    return store.insertBatch(records);
}

void TestDatabaseJobs::opensAndCreatesDatabase() {
    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const std::optional<DatabaseJobResult> result = waitForJob(finished, jobs.open(databasePath()));
    QVERIFY(result);
    QCOMPARE(result->kind, DatabaseJobResult::Kind::Open);
    QCOMPARE(result->databasePath, databasePath());
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QVERIFY(!jobs.isRunning());
    // The job created the schema, so the file opens as a LogVault database.
    const std::optional<QHash<QString, qint64>> counts = sourceCounts(databasePath());
    QVERIFY(counts);
    QVERIFY(counts->isEmpty());
}

void TestDatabaseJobs::reportsFileThatIsNotDatabase() {
    QFile file(databasePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("These are notes, not a database.\n") > 0);
    file.close();

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const std::optional<DatabaseJobResult> result = waitForJob(finished, jobs.open(databasePath()));
    QVERIFY(result);
    QVERIFY2(result->error.contains(u"is not a LogVault database"_s), qPrintable(result->error));
}

void TestDatabaseJobs::renamesSource() {
    QVERIFY(fill({u"cart"_s, u"payment"_s}, 3));
    const std::optional<qint64> id = sourceId(databasePath(), u"cart"_s);
    QVERIFY(id);

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const std::optional<DatabaseJobResult> result =
        waitForJob(finished, jobs.renameSource(databasePath(), *id, u"basket"_s));
    QVERIFY(result);
    QCOMPARE(result->kind, DatabaseJobResult::Kind::RenameSource);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    const QHash<QString, qint64> expected = {{u"basket"_s, 3}, {u"payment"_s, 3}};
    const std::optional<QHash<QString, qint64>> counts = sourceCounts(databasePath());
    QVERIFY(counts);
    QCOMPARE(*counts, expected);
}

void TestDatabaseJobs::reportsTakenName() {
    QVERIFY(fill({u"cart"_s, u"payment"_s}, 1));
    const std::optional<qint64> id = sourceId(databasePath(), u"cart"_s);
    QVERIFY(id);

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const std::optional<DatabaseJobResult> result =
        waitForJob(finished, jobs.renameSource(databasePath(), *id, u"payment"_s));
    QVERIFY(result);
    QVERIFY2(result->error.contains(u"already exists"_s), qPrintable(result->error));
}

void TestDatabaseJobs::deletesSourceWithItsRecords() {
    // More records than one deletion chunk.
    QVERIFY(fill({u"big"_s}, LogStore::kDeleteChunkSize + 500));
    QVERIFY(fill({u"small"_s}, 10));
    const std::optional<qint64> id = sourceId(databasePath(), u"big"_s);
    QVERIFY(id);

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const std::optional<DatabaseJobResult> result =
        waitForJob(finished, jobs.deleteSource(databasePath(), *id));
    QVERIFY(result);
    QCOMPARE(result->kind, DatabaseJobResult::Kind::DeleteSource);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->deleted, LogStore::kDeleteChunkSize + 500);
    QVERIFY(!result->cancelled);
    const QHash<QString, qint64> expected = {{u"small"_s, 10}};
    const std::optional<QHash<QString, qint64>> counts = sourceCounts(databasePath());
    QVERIFY(counts);
    QCOMPARE(*counts, expected);
}

void TestDatabaseJobs::cancelKeepsSourceAndRemainingRecords() {
    constexpr int kRecords = 3 * LogStore::kDeleteChunkSize;
    QVERIFY(fill({u"big"_s}, kRecords));
    const std::optional<qint64> id = sourceId(databasePath(), u"big"_s);
    QVERIFY(id);

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const bool started = jobs.deleteSource(databasePath(), *id);
    // The job opens its connection first, so the flag is set long before the first chunk is checked.
    jobs.cancel();
    const std::optional<DatabaseJobResult> result = waitForJob(finished, started);
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QVERIFY(result->cancelled);
    QVERIFY(result->deleted < kRecords);
    const QHash<QString, qint64> expected = {{u"big"_s, kRecords - result->deleted}};
    const std::optional<QHash<QString, qint64>> counts = sourceCounts(databasePath());
    QVERIFY(counts);
    QCOMPARE(*counts, expected);
}

void TestDatabaseJobs::deletesOldRecordsWithProgress() {
    // Times 1000 to 1000 + count - 1 in each source.
    constexpr int kRecords = LogStore::kDeleteChunkSize + 200;
    QVERIFY(fill({u"cart"_s, u"payment"_s}, kRecords));
    const std::optional<qint64> cartId = sourceId(databasePath(), u"cart"_s);
    QVERIFY(cartId);

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    // The signal comes from the pool thread; a context object in this thread queues it here.
    QObject context;
    QList<int> percents;
    connect(&jobs, &DatabaseJobs::progressChanged, &context,
            [&percents](int percent) { percents.append(percent); });

    std::optional<DatabaseJobResult> result =
        waitForJob(finished, jobs.deleteRecordsOlderThan(databasePath(), 1000 + 100, 0));
    QVERIFY(result);
    QCOMPARE(result->kind, DatabaseJobResult::Kind::DeleteOldRecords);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->deleted, 200);
    QVERIFY(!result->cancelled);
    QHash<QString, qint64> expected = {{u"cart"_s, kRecords - 100}, {u"payment"_s, kRecords - 100}};
    QCOMPARE(sourceCounts(databasePath()).value(), expected);
    QCOMPARE(percents, (QList<int>{0, 100}));

    // One source only, over two chunks.
    percents.clear();
    result = waitForJob(finished, jobs.deleteRecordsOlderThan(databasePath(), 1000 + kRecords, *cartId));
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->deleted, kRecords - 100);
    // Emptied sources stay.
    expected = {{u"cart"_s, 0}, {u"payment"_s, kRecords - 100}};
    QCOMPARE(sourceCounts(databasePath()).value(), expected);
    QCOMPARE(percents, (QList<int>{0, LogStore::kDeleteChunkSize * 100 / (kRecords - 100), 100}));
}

void TestDatabaseJobs::cancelKeepsOldRecordsNotReached() {
    constexpr int kRecords = 3 * LogStore::kDeleteChunkSize;
    QVERIFY(fill({u"big"_s}, kRecords));

    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    const bool started = jobs.deleteRecordsOlderThan(databasePath(), 1000 + kRecords, 0);
    jobs.cancel();
    const std::optional<DatabaseJobResult> result = waitForJob(finished, started);
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QVERIFY(result->cancelled);
    QVERIFY(result->deleted < kRecords);
    const QHash<QString, qint64> expected = {{u"big"_s, kRecords - result->deleted}};
    QCOMPARE(sourceCounts(databasePath()).value(), expected);
}

void TestDatabaseJobs::runsOneJobAtATime() {
    DatabaseJobs jobs;
    QSignalSpy finished(&jobs, &DatabaseJobs::finished);
    QVERIFY(jobs.open(databasePath()));
    QVERIFY(jobs.isRunning());
    QVERIFY(!jobs.open(databasePath()));
    QVERIFY(!jobs.renameSource(databasePath(), 1, u"x"_s));
    QVERIFY(finished.wait(kWaitMs));
    QCOMPARE(finished.count(), 1);
    QVERIFY(!jobs.isRunning());
}
