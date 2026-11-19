#include "tst_searchservice.h"

#include "Database.h"
#include "LogStore.h"
#include "Record.h"
#include "SearchService.h"

#include <QCoreApplication>
#include <QFile>
#include <QMetaType>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>
#include <optional>

namespace {

constexpr int kWaitMs = 60'000;
// 2026-11-20 10:00:00 UTC.
constexpr qint64 kT = 1'795'168'800'000;

// count records a second apart from firstTs, cycling through every level, all of the source "app".
QVector<Record> makeRecords(int count, qint64 firstTs) {
    QVector<Record> records;
    records.reserve(count);
    for (int i = 0; i < count; ++i) {
        Record record;
        record.ts = firstTs + qint64(i) * 1000;
        record.level = levelFromValue(i % kLevelCount);
        record.source = QStringLiteral("app");
        record.message = QStringLiteral("Request %1 done").arg(i);
        record.raw = record.message;
        records.append(record);
    }
    return records;
}

bool writeRecords(const QString& path, const QVector<Record>& records, QString* error) {
    Database database;
    if (!database.open(path)) {
        *error = database.errorString();
        return false;
    }
    LogStore store(database);
    if (!store.insertBatch(records)) {
        *error = store.errorString();
        return false;
    }
    return true;
}

// Runs a search to its end; nullopt if no result arrives in time.
std::optional<SearchResult> runSearch(SearchService& service, const SearchRequest& request) {
    QSignalSpy finished(&service, &SearchService::finished);
    const quint64 generation = service.search(request);
    if (!finished.wait(kWaitMs)) {
        return std::nullopt;
    }
    const SearchResult result = finished.first().first().value<SearchResult>();
    if (result.generation != generation) {
        return std::nullopt;
    }
    return result;
}

} // namespace

void TestSearchService::init() {
    // A warning means something was not shut down cleanly (a pool task, a connection still in use).
    QTest::failOnWarning();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestSearchService::cleanup() { m_dir.reset(); }

QString TestSearchService::databasePath(const QString& fileName) const { return m_dir->filePath(fileName); }

void TestSearchService::deliversResult() {
    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(50, kT), &error), qPrintable(error));

    SearchService service;
    // The result types are known by name, so they can travel through any queued connection.
    QVERIFY(QMetaType::fromName("SearchResult").isValid());
    QVERIFY(QMetaType::fromName("Histogram").isValid());
    QVERIFY(QMetaType::fromName("StatsRow").isValid());

    service.setDatabasePath(databasePath());
    QCOMPARE(service.databasePath(), databasePath());
    SearchRequest request;
    request.maxRows = 10;
    std::optional<SearchResult> result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->total, 50);
    QCOMPARE(result->rows.size(), 10);
    QCOMPARE(result->rows.first().ts, kT + 49'000);
    QCOMPARE(result->rows.first().message, QStringLiteral("Request 49 done"));
    QCOMPARE(result->histogram.start, kT);
    QCOMPARE(result->histogram.step, 1000);
    QCOMPARE(result->histogram.bars.size(), 50);
    QCOMPARE(result->histogram.total(), 50);
    QVERIFY(result->stats.isEmpty());
    QVERIFY(result->elapsedMs >= 0);

    // Every search gets the next number.
    const quint64 previous = result->generation;
    request.text = QStringLiteral("\"Request 7 \"");
    result = runSearch(service, request);
    QVERIFY(result);
    QCOMPARE(result->generation, previous + 1);
    QCOMPARE(result->total, 1);
    QCOMPARE(result->rows.first().message, QStringLiteral("Request 7 done"));
}

void TestSearchService::computesStatisticsOnlyWhenAsked() {
    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(50, kT), &error), qPrintable(error));
    SearchService service;
    service.setDatabasePath(databasePath());

    SearchRequest request;
    request.statsMode = StatsMode::Level;
    std::optional<SearchResult> result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->stats.size(), kLevelCount);
    qint64 counted = 0;
    for (const StatsRow& row : result->stats) {
        counted += row.count;
    }
    QCOMPARE(counted, 50);

    request.statsMode = StatsMode::Source;
    result = runSearch(service, request);
    QVERIFY(result);
    QCOMPARE(result->stats.size(), 1);
    QCOMPARE(result->stats.first().value, QStringLiteral("app"));
    QCOMPARE(result->stats.first().count, 50);

    // No record has fields, so all of them are in the "(no field)" group, counted from the total.
    request.statsMode = StatsMode::Field;
    request.statsField = QStringLiteral("provider");
    result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->stats.size(), 1);
    QVERIFY(result->stats.first().noField);
    QCOMPARE(result->stats.first().count, 50);

    request.statsMode = StatsMode::None;
    result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY(result->stats.isEmpty());
}

void TestSearchService::dropsStaleResults() {
    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(2000, kT), &error), qPrintable(error));
    SearchService service;
    service.setDatabasePath(databasePath());
    QSignalSpy finished(&service, &SearchService::finished);

    // Started before any result could be delivered, so only the last one may arrive.
    SearchRequest request;
    request.statsMode = StatsMode::Message;
    quint64 latest = 0;
    for (int i = 0; i < 5; ++i) {
        request.text = QStringLiteral("\"Request %1 \"").arg(i);
        const quint64 generation = service.search(request);
        QVERIFY(generation > latest);
        latest = generation;
    }
    service.waitForFinished();
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), 1);
    const SearchResult result = finished.first().first().value<SearchResult>();
    QCOMPARE(result.generation, latest);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.total, 1);
    QCOMPARE(result.rows.first().message, QStringLiteral("Request 4 done"));
}

void TestSearchService::dropsResultsOfPreviousDatabase() {
    QString error;
    QVERIFY2(writeRecords(databasePath(QStringLiteral("a.lvdb")), makeRecords(50, kT), &error),
             qPrintable(error));
    QVERIFY2(writeRecords(databasePath(QStringLiteral("b.lvdb")), makeRecords(5, kT), &error),
             qPrintable(error));
    SearchService service;
    QSignalSpy finished(&service, &SearchService::finished);

    service.setDatabasePath(databasePath(QStringLiteral("a.lvdb")));
    service.search(SearchRequest());
    service.setDatabasePath(databasePath(QStringLiteral("b.lvdb")));
    service.waitForFinished();
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), 0);

    const std::optional<SearchResult> result = runSearch(service, SearchRequest());
    QVERIFY(result);
    QCOMPARE(result->total, 5);
}

void TestSearchService::reportsErrors() {
    SearchService service;
    const auto expectError = [&service](const QString& part) {
        const std::optional<SearchResult> result = runSearch(service, SearchRequest());
        QVERIFY(result);
        QVERIFY2(result->error.contains(part), qPrintable(result->error));
        QVERIFY(result->rows.isEmpty());
        QCOMPARE(result->total, 0);
        QVERIFY(result->histogram.bars.isEmpty());
    };

    expectError(QStringLiteral("No database is open."));

    // A missing file is reported, not created.
    const QString missing = databasePath(QStringLiteral("missing.lvdb"));
    service.setDatabasePath(missing);
    expectError(QStringLiteral("does not exist"));
    QVERIFY(!QFile::exists(missing));

    const QString text = databasePath(QStringLiteral("text.lvdb"));
    QFile file(text);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write("this is not a database\n") > 0);
    file.close();
    service.setDatabasePath(text);
    expectError(QStringLiteral("is not a LogVault database"));

    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(5, kT), &error), qPrintable(error));
    service.setDatabasePath(databasePath());
    SearchRequest request;
    request.statsMode = StatsMode::Field;
    request.statsField = QStringLiteral("not valid");
    const std::optional<SearchResult> result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY2(result->error.contains(QStringLiteral("not a valid field name")), qPrintable(result->error));
    QVERIFY(result->rows.isEmpty());
}

void TestSearchService::keepsBarsOfEmptyBoundedPeriod() {
    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(10, kT), &error), qPrintable(error));
    SearchService service;
    service.setDatabasePath(databasePath());

    SearchRequest request;
    request.text = QStringLiteral("no-such-text");
    request.from = kT;
    request.to = kT + 60'000;
    std::optional<SearchResult> result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
    QCOMPARE(result->total, 0);
    QVERIFY(result->rows.isEmpty());
    // The empty period still has its time axis.
    QCOMPARE(result->histogram.start, kT);
    QCOMPARE(result->histogram.step, 1000);
    QCOMPARE(result->histogram.bars.size(), 60);
    QCOMPARE(result->histogram.total(), 0);

    request.from = 0;
    result = runSearch(service, request);
    QVERIFY(result);
    QVERIFY(result->histogram.bars.isEmpty());
    QCOMPARE(result->histogram.step, 0);
}

void TestSearchService::searchesWhileRecordsArrive() {
    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(1000, kT), &error), qPrintable(error));

    // Another connection keeps writing, as intake does while the user searches.
    std::atomic<bool> writeFailed = false;
    const QString path = databasePath();
    std::unique_ptr<QThread> writer(QThread::create([path, &writeFailed] {
        Database database;
        if (!database.open(path)) {
            writeFailed = true;
            return;
        }
        LogStore store(database);
        for (int batch = 0; batch < 100; ++batch) {
            if (!store.insertBatch(makeRecords(200, kT + 1'000'000 + qint64(batch) * 200'000))) {
                writeFailed = true;
                return;
            }
        }
    }));
    writer->start();

    SearchService service;
    service.setDatabasePath(databasePath());
    SearchRequest request;
    request.maxRows = 500;
    int searches = 0;
    qint64 lastTotal = 0;
    while (!writer->isFinished() || searches < 3) {
        const std::optional<SearchResult> result = runSearch(service, request);
        QVERIFY(result);
        QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
        // One read transaction: the total, the rows and the bars describe the same records.
        QCOMPARE(result->histogram.total(), result->total);
        QCOMPARE(result->rows.size(), qMin<qint64>(result->total, request.maxRows));
        QVERIFY(std::is_sorted(result->rows.begin(), result->rows.end(),
                               [](const Record& a, const Record& b) { return a.ts > b.ts; }));
        QVERIFY(result->total >= lastTotal);
        lastTotal = result->total;
        ++searches;
    }
    QVERIFY(writer->wait(kWaitMs));
    QVERIFY(!writeFailed);
    QCOMPARE(runSearch(service, request).value().total, 1000 + 100 * 200);
}

void TestSearchService::destroysWhileSearching() {
    QString error;
    QVERIFY2(writeRecords(databasePath(), makeRecords(20'000, kT), &error), qPrintable(error));

    auto service = std::make_unique<SearchService>();
    QSignalSpy finished(service.get(), &SearchService::finished);
    service->setDatabasePath(databasePath());
    SearchRequest request;
    request.statsMode = StatsMode::Message;
    service->search(request);
    service->search(request);
    // The destructor waits for both searches; nothing is delivered afterwards and no connection is left.
    service.reset();
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), 0);
    for (const QString& name : QSqlDatabase::connectionNames()) {
        QVERIFY2(!name.startsWith(QStringLiteral("logvault-")), qPrintable(name));
    }
}
