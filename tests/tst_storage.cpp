#include "tst_storage.h"

#include "ConnectionGuard.h"
#include "Database.h"
#include "LogStore.h"
#include "Record.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <array>
#include <memory>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<Database> && !std::is_copy_assignable_v<Database>,
              "a Database owns its connection and must not be copied");
static_assert(!std::is_copy_constructible_v<LogStore> && !std::is_copy_assignable_v<LogStore>,
              "a LogStore holds prepared queries of one connection and must not be copied");

namespace {

Record makeRecord(qint64 ts, const QString& source, const QString& message, Level level = Level::Info) {
    Record record;
    record.ts = ts;
    record.level = level;
    record.source = source;
    record.message = message;
    record.raw = message;
    return record;
}

// Records with increasing times starting at firstTs, all of one source.
QVector<Record> makeBatch(int count, qint64 firstTs, const QString& source) {
    QVector<Record> batch;
    batch.reserve(count);
    for (int i = 0; i < count; ++i) {
        batch.append(
            makeRecord(firstTs + i, source, QStringLiteral("Request %1 done in %2 ms").arg(i).arg(i % 97)));
    }
    return batch;
}

// The source with this name, or a SourceInfo with id 0 if there is none.
SourceInfo findSource(const QVector<SourceInfo>& sources, const QString& name) {
    for (const SourceInfo& source : sources) {
        if (source.name == name) {
            return source;
        }
    }
    return {};
}

// Reads the journal mode of a file through a separate plain connection.
QString journalModeOf(const QString& path) {
    const QString connection = QStringLiteral("tst_storage_journal");
    const ConnectionGuard guard(connection);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(path);
    if (!db.open()) {
        return db.lastError().text();
    }
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode")) || !query.next()) {
        return query.lastError().text();
    }
    return query.value(0).toString();
}

} // namespace

void TestStorage::init() {
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestStorage::cleanup() { m_dir.reset(); }

QString TestStorage::databasePath(const QString& fileName) const { return m_dir->filePath(fileName); }

void TestStorage::levelNamesAndValues() {
    QCOMPARE(levelName(Level::Unknown), QStringLiteral("UNKNOWN"));
    QCOMPARE(levelName(Level::Trace), QStringLiteral("TRACE"));
    QCOMPARE(levelName(Level::Debug), QStringLiteral("DEBUG"));
    QCOMPARE(levelName(Level::Info), QStringLiteral("INFO"));
    QCOMPARE(levelName(Level::Warn), QStringLiteral("WARN"));
    QCOMPARE(levelName(Level::Error), QStringLiteral("ERROR"));
    QCOMPARE(levelName(Level::Fatal), QStringLiteral("FATAL"));

    // The stored values are part of the file format.
    QCOMPARE(static_cast<int>(Level::Unknown), 0);
    QCOMPARE(static_cast<int>(Level::Fatal), 6);
    QCOMPARE(levelFromValue(4), Level::Warn);
    QCOMPARE(levelFromValue(-1), Level::Unknown);
    QCOMPARE(levelFromValue(7), Level::Unknown);
}

void TestStorage::messageKeyGroupsDigits_data() {
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("key");

    QTest::newRow("design example") << QStringLiteral("Timeout after 3000 ms for order 8812")
                                    << QStringLiteral("Timeout after # ms for order #");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("no digits") << QStringLiteral("Connection reset") << QStringLiteral("Connection reset");
    QTest::newRow("only digits") << QStringLiteral("12345") << QStringLiteral("#");
    QTest::newRow("glued to letters") << QStringLiteral("a1b22c333") << QStringLiteral("a#b#c#");
    QTest::newRow("separators split runs") << QStringLiteral("10.0.0.1:8080") << QStringLiteral("#.#.#.#:#");
    QTest::newRow("long message is cut") << QString(300, u'x') << QString(200, u'x');
    QTest::newRow("cut after replacing")
        << QString(500, u'7') + QString(250, u'y') << u'#' + QString(199, u'y');
}

void TestStorage::messageKeyGroupsDigits() {
    QFETCH(QString, message);
    QFETCH(QString, key);
    QCOMPARE(messageKey(message), key);
}

void TestStorage::messageKeyKeepsSurrogatePairsWhole() {
    // U+1F600 is two UTF-16 code units; at positions 199-200 the cut would split it.
    const QString emoji = QStringLiteral(u"\U0001F600");
    QCOMPARE(emoji.size(), 2);
    const QString message = QString(199, u'x') + emoji + QStringLiteral("tail");
    QCOMPARE(messageKey(message), QString(199, u'x'));

    // When the pair fits completely, it is kept.
    const QString fits = QString(198, u'x') + emoji + QStringLiteral("tail");
    QCOMPARE(messageKey(fits), QString(198, u'x') + emoji);
}

void TestStorage::createsSchemaAndReopens() {
    const QString path = databasePath();
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
        QVERIFY(db.isOpen());
        QCOMPARE(db.path(), path);
        QVERIFY(!db.open(path)); // one object, one connection
    }
    QVERIFY(QFileInfo::exists(path));

    Database db;
    QVERIFY2(db.open(path), qPrintable(db.errorString()));
    QSqlQuery query(db.connection());
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString::number(Database::kSchemaVersion));
    QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("wal"));
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestStorage::rejectsNewerSchemaVersion() {
    const QString path = databasePath();
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
        QSqlQuery query(db.connection());
        QVERIFY(query.exec(QStringLiteral("UPDATE meta SET value = '2' WHERE key = 'schema_version'")));
    }

    Database db;
    QVERIFY(!db.open(path));
    QVERIFY(!db.isOpen());
    QVERIFY2(db.errorString().contains(QStringLiteral("newer version")), qPrintable(db.errorString()));
}

void TestStorage::rejectsForeignSqliteFile() {
    const QString path = databasePath(QStringLiteral("foreign.db"));
    {
        const QString connection = QStringLiteral("tst_storage_foreign");
        const ConnectionGuard guard(connection);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE notes (text TEXT)")));
    }

    Database db;
    QVERIFY(!db.open(path));
    QVERIFY2(db.errorString().contains(QStringLiteral("not a LogVault database")),
             qPrintable(db.errorString()));
    // The foreign file must stay untouched, including its journal mode.
    QCOMPARE(journalModeOf(path), QStringLiteral("delete"));
}

void TestStorage::rejectsNonSqliteFile() {
    const QString path = databasePath(QStringLiteral("notes.txt"));
    const QByteArray content = QByteArray("This is a plain text file, not a database.\n").repeated(40);
    {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(content), content.size());
    }

    Database db;
    QVERIFY(!db.open(path));
    QVERIFY2(db.errorString().contains(QStringLiteral("not a LogVault database")),
             qPrintable(db.errorString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), content);
}

void TestStorage::rejectsMissingDirectory() {
    Database db;
    QVERIFY(!db.open(databasePath(QStringLiteral("missing/sub/test.lvdb"))));
    QVERIFY(!db.errorString().isEmpty());
    QVERIFY(!db.open(QString()));
    QVERIFY(!db.errorString().isEmpty());
}

void TestStorage::opensPathWithSpacesAndNonAscii() {
    // "test dir" and "journal.lvdb" in Russian, written with escapes to keep the source ASCII.
    const QString dirName = QStringLiteral(u"\u0442\u0435\u0441\u0442 dir");
    QVERIFY(QDir(m_dir->path()).mkdir(dirName));
    const QString path =
        QDir(m_dir->filePath(dirName)).filePath(QStringLiteral(u"\u0436\u0443\u0440\u043D\u0430\u043B.lvdb"));
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
    }
    QVERIFY(QFileInfo::exists(path));

    Database db;
    QVERIFY2(db.open(path), qPrintable(db.errorString()));
}

void TestStorage::reopenKeepsRecords() {
    const QString path = databasePath();
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
        LogStore store(db);
        QVERIFY2(store.insertBatch(makeBatch(10, 1000, QStringLiteral("api"))),
                 qPrintable(store.errorString()));
    }

    Database db;
    QVERIFY2(db.open(path), qPrintable(db.errorString()));
    LogStore store(db);
    const std::optional<qint64> count = store.recordCount();
    QVERIFY2(count, qPrintable(store.errorString()));
    QCOMPARE(*count, 10);
    const std::optional<QVector<Record>> latest = store.latestRecords(1);
    QVERIFY2(latest, qPrintable(store.errorString()));
    QCOMPARE(latest->size(), 1);
    QCOMPARE(latest->first().ts, 1009);
    QCOMPARE(latest->first().source, QStringLiteral("api"));
}

void TestStorage::insertsHundredThousandRecordsQuickly() {
    constexpr int kBatchSize = 500;
    constexpr int kTotal = 100'000;
    const QStringList sources = {QStringLiteral("api"), QStringLiteral("auth"), QStringLiteral("db"),
                                 QStringLiteral("worker")};

    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);

    // Prepared outside the timer, so only the database work is measured.
    QVector<QVector<Record>> batches;
    for (int first = 0; first < kTotal; first += kBatchSize) {
        QVector<Record> batch;
        batch.reserve(kBatchSize);
        for (int i = first; i < first + kBatchSize; ++i) {
            Record record = makeRecord(1'700'000'000'000 + i, sources.at(i % sources.size()),
                                       QStringLiteral("Order %1 processed in %2 ms").arg(i).arg(i % 500),
                                       levelFromValue(i % 7));
            record.fields = QStringLiteral(R"({"order":%1,"http.status":200})").arg(i);
            batch.append(record);
        }
        batches.append(batch);
    }

    QElapsedTimer timer;
    timer.start();
    for (const QVector<Record>& batch : batches) {
        QVERIFY2(store.insertBatch(batch), qPrintable(store.errorString()));
    }
    const qint64 elapsed = timer.elapsed();
    qInfo("Inserted %d records in batches of %d in %lld ms", kTotal, kBatchSize, elapsed);
    QVERIFY2(elapsed < 5000, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));

    const std::optional<qint64> count = store.recordCount();
    QVERIFY2(count, qPrintable(store.errorString()));
    QCOMPARE(*count, kTotal);
}

void TestStorage::createsSourcesWithCounts() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);

    // One batch with several sources, then a batch that reuses an existing one.
    QVector<Record> batch = makeBatch(3, 0, QStringLiteral("billing"));
    batch += makeBatch(5, 0, QStringLiteral("api"));
    batch.append(makeRecord(10, QStringLiteral("billing"), QStringLiteral("late")));
    QVERIFY2(store.insertBatch(batch), qPrintable(store.errorString()));
    QVERIFY2(store.insertBatch(makeBatch(2, 20, QStringLiteral("api"))), qPrintable(store.errorString()));

    const std::optional<QVector<SourceInfo>> sources = store.sources();
    QVERIFY2(sources, qPrintable(store.errorString()));
    QCOMPARE(sources->size(), 2);
    QCOMPARE(sources->at(0).name, QStringLiteral("api")); // sorted by name
    QCOMPARE(sources->at(0).recordCount, 7);
    QCOMPARE(sources->at(1).name, QStringLiteral("billing"));
    QCOMPARE(sources->at(1).recordCount, 4);
    QVERIFY(sources->at(0).id != sources->at(1).id);
}

void TestStorage::storesEmptyTextsAndSkipsEmptyBatch() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);

    QVERIFY(store.insertBatch({}));
    // A default Record has null strings; they must be stored as empty texts, not rejected as NULL.
    QVERIFY2(store.insertBatch({Record()}), qPrintable(store.errorString()));

    const std::optional<QVector<Record>> latest = store.latestRecords(10);
    QVERIFY2(latest, qPrintable(store.errorString()));
    QCOMPARE(latest->size(), 1);
    QCOMPARE(latest->first().level, Level::Unknown);
    QVERIFY(latest->first().message.isEmpty());
    QVERIFY(latest->first().fields.isEmpty());
    QVERIFY(latest->first().source.isEmpty());
}

void TestStorage::failedBatchWritesNothing() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);
    QVERIFY2(store.insertBatch(makeBatch(2, 0, QStringLiteral("api"))), qPrintable(store.errorString()));
    {
        // A test-only trigger makes one record fail in the middle of a batch.
        QSqlQuery query(db.connection());
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TRIGGER poison BEFORE INSERT ON records "
                     "WHEN NEW.message = 'poison' BEGIN SELECT RAISE(ABORT, 'poisoned'); END")),
                 qPrintable(query.lastError().text()));
    }

    QVector<Record> batch = makeBatch(3, 10, QStringLiteral("new source"));
    batch.append(makeRecord(20, QStringLiteral("api"), QStringLiteral("poison")));
    batch += makeBatch(3, 30, QStringLiteral("api"));
    QVERIFY(!store.insertBatch(batch));
    QVERIFY2(store.errorString().contains(QStringLiteral("poisoned")), qPrintable(store.errorString()));

    // Neither the records nor the source created earlier in the same batch remain.
    QCOMPARE(store.recordCount().value(), 2);
    QCOMPARE(store.sources().value().size(), 1);

    // The transaction was closed: the next batch is written normally.
    QVERIFY2(store.insertBatch(makeBatch(3, 10, QStringLiteral("new source"))),
             qPrintable(store.errorString()));
    QCOMPARE(store.recordCount().value(), 5);
}

void TestStorage::renamesSource() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);
    QVERIFY2(store.insertBatch(makeBatch(4, 0, QStringLiteral("old name"))), qPrintable(store.errorString()));
    const qint64 id = findSource(store.sources().value(), QStringLiteral("old name")).id;
    QVERIFY(id != 0);

    QVERIFY2(store.renameSource(id, QStringLiteral("  new name  ")), qPrintable(store.errorString()));
    // Renaming to the current name is allowed and changes nothing.
    QVERIFY2(store.renameSource(id, QStringLiteral("new name")), qPrintable(store.errorString()));

    const QVector<SourceInfo> sources = store.sources().value();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.first().id, id);
    QCOMPARE(sources.first().name, QStringLiteral("new name"));
    QCOMPARE(sources.first().recordCount, 4);

    // New records with the old name start a new source instead of joining the renamed one.
    QVERIFY2(store.insertBatch(makeBatch(1, 10, QStringLiteral("old name"))),
             qPrintable(store.errorString()));
    QCOMPARE(store.sources().value().size(), 2);
}

void TestStorage::rejectsBadRenames() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);
    QVector<Record> batch = makeBatch(2, 0, QStringLiteral("api"));
    batch += makeBatch(3, 0, QStringLiteral("db"));
    QVERIFY2(store.insertBatch(batch), qPrintable(store.errorString()));
    const QVector<SourceInfo> before = store.sources().value();
    const qint64 apiId = findSource(before, QStringLiteral("api")).id;

    QVERIFY(!store.renameSource(apiId, QStringLiteral("db")));
    QVERIFY2(store.errorString().contains(QStringLiteral("already exists")), qPrintable(store.errorString()));
    QVERIFY(!store.renameSource(apiId, QStringLiteral("   ")));
    QVERIFY(!store.errorString().isEmpty());
    QVERIFY(!store.renameSource(999, QStringLiteral("ghost")));
    QVERIFY(!store.errorString().isEmpty());

    const QVector<SourceInfo> after = store.sources().value();
    QCOMPARE(after.size(), before.size());
    QCOMPARE(findSource(after, QStringLiteral("api")).recordCount, 2);
    QCOMPARE(findSource(after, QStringLiteral("db")).recordCount, 3);

    // A failed rename leaves no transaction open: the next write works.
    QVERIFY2(store.insertBatch(makeBatch(1, 5, QStringLiteral("api"))), qPrintable(store.errorString()));
}

void TestStorage::deleteSourceRemovesItsRecords() {
    // More than two chunks, so the chunked loop runs several transactions.
    constexpr int kDeleted = LogStore::kDeleteChunkSize * 2 + 500;
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);
    QVERIFY2(store.insertBatch(makeBatch(kDeleted, 0, QStringLiteral("noisy"))),
             qPrintable(store.errorString()));
    QVERIFY2(store.insertBatch(makeBatch(7, 0, QStringLiteral("quiet"))), qPrintable(store.errorString()));
    const qint64 noisyId = findSource(store.sources().value(), QStringLiteral("noisy")).id;

    const std::optional<qint64> deleted = store.deleteSource(noisyId);
    QVERIFY2(deleted, qPrintable(store.errorString()));
    QCOMPARE(*deleted, kDeleted);

    const QVector<SourceInfo> sources = store.sources().value();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.first().name, QStringLiteral("quiet"));
    QCOMPARE(store.recordCount().value(), 7);

    // Deleting a source that no longer exists is not an error.
    QCOMPARE(store.deleteSource(noisyId).value_or(-1), 0);

    // The schema cascades as well: records still present when the source row goes are removed with it.
    QVERIFY2(store.insertBatch(makeBatch(3, 0, QStringLiteral("late"))), qPrintable(store.errorString()));
    {
        QSqlQuery query(db.connection());
        QVERIFY2(query.exec(QStringLiteral("DELETE FROM sources WHERE name = 'late'")),
                 qPrintable(query.lastError().text()));
    }
    QCOMPARE(store.recordCount().value(), 7);
}

void TestStorage::deleteSourceCanBeCancelled() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);
    QVERIFY2(store.insertBatch(makeBatch(100, 0, QStringLiteral("api"))), qPrintable(store.errorString()));
    const qint64 id = findSource(store.sources().value(), QStringLiteral("api")).id;

    const std::atomic<bool> cancelled = true;
    QCOMPARE(store.deleteSource(id, &cancelled).value_or(-1), 0);
    QCOMPARE(findSource(store.sources().value(), QStringLiteral("api")).recordCount, 100);

    const std::atomic<bool> running = false;
    QCOMPARE(store.deleteSource(id, &running).value_or(-1), 100);
    QVERIFY(store.sources().value().isEmpty());
}

void TestStorage::deleteOlderThanIsStrict() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);
    QVector<Record> batch;
    for (const qint64 ts : {999, 1000, 1001}) {
        batch.append(makeRecord(ts, QStringLiteral("a"), QStringLiteral("a")));
        batch.append(makeRecord(ts, QStringLiteral("b"), QStringLiteral("b")));
    }
    QVERIFY2(store.insertBatch(batch), qPrintable(store.errorString()));
    const qint64 aId = findSource(store.sources().value(), QStringLiteral("a")).id;

    // Only one source: its record at 999 goes, the one exactly at 1000 stays.
    QCOMPARE(store.deleteRecordsOlderThan(1000, aId).value_or(-1), 1);
    QCOMPARE(findSource(store.sources().value(), QStringLiteral("a")).recordCount, 2);
    QCOMPARE(findSource(store.sources().value(), QStringLiteral("b")).recordCount, 3);

    // All sources.
    QCOMPARE(store.deleteRecordsOlderThan(1000).value_or(-1), 1);
    const QVector<Record> left = store.latestRecords(10).value();
    QCOMPARE(left.size(), 4);
    for (const Record& record : left) {
        QVERIFY(record.ts >= 1000);
    }
    // Emptied sources stay until deleted explicitly.
    QCOMPARE(store.deleteRecordsOlderThan(5000).value_or(-1), 4);
    QCOMPARE(store.sources().value().size(), 2);
}

void TestStorage::unicodeRoundTrip() {
    Database db;
    QVERIFY2(db.open(databasePath()), qPrintable(db.errorString()));
    LogStore store(db);

    // Cyrillic, CJK, an emoji outside the BMP and a quote, written with escapes to keep the source ASCII.
    Record record;
    record.ts = 1'763'560'000'123;
    record.level = Level::Error;
    record.source = QStringLiteral(u"\u0441\u0435\u0440\u0432\u0438\u0441-\u65E5\u672C");
    record.message =
        QStringLiteral(u"\u041E\u0448\u0438\u0431\u043A\u0430 42 \u2014 \"\U0001F525\" \u6587\u5B57");
    record.fields = QStringLiteral(u"{\"user\":\"\u0418\u0432\u0430\u043D\"}");
    record.raw = QStringLiteral(u"raw: ") + record.message;
    QVERIFY2(store.insertBatch({record}), qPrintable(store.errorString()));

    const std::optional<QVector<Record>> latest = store.latestRecords(1);
    QVERIFY2(latest, qPrintable(store.errorString()));
    QCOMPARE(latest->size(), 1);
    const Record& stored = latest->first();
    QVERIFY(stored.id > 0);
    QCOMPARE(stored.ts, record.ts);
    QCOMPARE(stored.level, record.level);
    QCOMPARE(stored.source, record.source);
    QCOMPARE(stored.message, record.message);
    QCOMPARE(stored.fields, record.fields);
    QCOMPARE(stored.raw, record.raw);
    QCOMPARE(store.sources().value().first().name, record.source);
}

void TestStorage::writersAndReaderInParallel() {
    constexpr int kWriters = 2;
    constexpr int kBatches = 60;
    constexpr int kBatchSize = 500;
    const QString path = databasePath();

    // The reader opens first and creates the schema; each writer then opens its own connection.
    Database readerDb;
    QVERIFY2(readerDb.open(path), qPrintable(readerDb.errorString()));
    LogStore reader(readerDb);

    std::array<QString, kWriters> writerErrors;
    std::array<std::unique_ptr<QThread>, kWriters> writers;
    for (int w = 0; w < kWriters; ++w) {
        writers[w].reset(QThread::create([&path, &error = writerErrors[w], w] {
            Database db;
            if (!db.open(path)) {
                error = db.errorString();
                return;
            }
            LogStore store(db);
            const QString source = QStringLiteral("writer-%1").arg(w);
            for (int b = 0; b < kBatches; ++b) {
                if (!store.insertBatch(makeBatch(kBatchSize, qint64(b) * kBatchSize, source))) {
                    error = store.errorString();
                    return;
                }
            }
        }));
        writers[w]->start();
    }

    // Errors are collected instead of checked at once: the threads must be joined before the test returns.
    QString readerError;
    qint64 lastCount = 0;
    int reads = 0;
    const auto anyRunning = [&writers] {
        return std::any_of(writers.begin(), writers.end(), [](const auto& t) { return !t->isFinished(); });
    };
    while (anyRunning() && readerError.isEmpty()) {
        const std::optional<qint64> count = reader.recordCount();
        if (!count) {
            readerError = reader.errorString();
        } else if (*count < lastCount) {
            readerError = QStringLiteral("count went down from %1 to %2").arg(lastCount).arg(*count);
        } else {
            lastCount = *count;
            ++reads;
        }
    }
    for (const auto& writer : writers) {
        writer->wait();
    }

    for (const QString& error : writerErrors) {
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    QVERIFY2(readerError.isEmpty(), qPrintable(readerError));
    qInfo("Reader made %d successful reads while the writers worked", reads);
    QCOMPARE(reader.recordCount().value(), kWriters * kBatches * kBatchSize);
    const QVector<SourceInfo> sources = reader.sources().value();
    QCOMPARE(sources.size(), kWriters);
    for (const SourceInfo& source : sources) {
        QCOMPARE(source.recordCount, kBatches * kBatchSize);
    }
}
