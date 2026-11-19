#include "tst_fileimporter.h"

#include "Database.h"
#include "FileImporter.h"
#include "LineSplitter.h"
#include "LogStore.h"
#include "StoredRecords.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <optional>

namespace {

constexpr int kWaitMs = 120'000;

// Runs an import to its end; nullopt if it does not start or does not finish in time.
std::optional<ImportSummary> runImport(FileImporter& importer, const QString& databasePath,
                                       const QVector<ImportFile>& files) {
    QSignalSpy finished(&importer, &FileImporter::finished);
    if (!importer.start(databasePath, files) || !finished.wait(kWaitMs)) {
        return std::nullopt;
    }
    return finished.first().first().value<ImportSummary>();
}

QHash<QString, int> countBySource(const QVector<Record>& records) {
    QHash<QString, int> counts;
    for (const Record& record : records) {
        ++counts[record.source];
    }
    return counts;
}

} // namespace

void TestFileImporter::init() {
    // A warning means something was not shut down cleanly (a pool task, a connection still in use).
    QTest::failOnWarning();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestFileImporter::cleanup() { m_dir.reset(); }

QString TestFileImporter::databasePath() const { return m_dir->filePath(QStringLiteral("import.lvdb")); }

QString TestFileImporter::writeFile(const QString& name, const QByteArray& content) const {
    const QString path = m_dir->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        return {};
    }
    return path;
}

void TestFileImporter::importsCrlfAndBom() {
    const QString path = writeFile(QStringLiteral("crlf.log"), "\xEF\xBB\xBF"
                                                               R"({"level":"info","msg":"first"})"
                                                               "\r\n"
                                                               "2026-11-19 14:05:01 WARN second\r\n"
                                                               "\r\n"
                                                               "ERROR third without a line break");
    QVERIFY(!path.isEmpty());
    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{path, QStringLiteral("app")}});
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->added, 3);
    QCOMPARE(summary->unparsed, 0);
    QVERIFY(!summary->cancelled);
    QVERIFY(!importer.isRunning());

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 3);
    // The BOM is gone, so the first line still parses as JSON.
    QCOMPARE(records[0].message, QStringLiteral("first"));
    QCOMPARE(records[0].raw, QStringLiteral(R"({"level":"info","msg":"first"})"));
    QCOMPARE(records[1].level, Level::Warn);
    QCOMPARE(records[1].raw, QStringLiteral("2026-11-19 14:05:01 WARN second"));
    QCOMPARE(records[2].level, Level::Error);
    QCOMPARE(records[2].message, QStringLiteral("third without a line break"));
    QCOMPARE(countBySource(records).value(QStringLiteral("app")), 3);
}

void TestFileImporter::joinsIndentedLines() {
    const QString path = writeFile(QStringLiteral("trace.log"), "  indented first line\n"
                                                                "2026-11-19 14:05:01 ERROR Request failed\r\n"
                                                                "    at com.shop.Cart.add(Cart.java:42)\r\n"
                                                                "\n"
                                                                "\tat com.shop.Api.handle(Api.java:7)\n"
                                                                "2026-11-19 14:05:02 INFO next\n"
                                                                R"({"level":"error","msg":"json"})"
                                                                "\n"
                                                                "  joined to json\n");
    QVERIFY(!path.isEmpty());
    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{path, QStringLiteral("app")}});
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->added, 4);
    QCOMPARE(summary->unparsed, 1);

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 4);
    // An indented first line has nothing to join, so it is a record of its own.
    QCOMPARE(records[0].raw, QStringLiteral("  indented first line"));
    QCOMPARE(records[1].level, Level::Error);
    QCOMPARE(records[1].message, QStringLiteral("Request failed\n"
                                                "    at com.shop.Cart.add(Cart.java:42)\n"
                                                "\tat com.shop.Api.handle(Api.java:7)"));
    QCOMPARE(records[1].raw, QStringLiteral("2026-11-19 14:05:01 ERROR Request failed\n"
                                            "    at com.shop.Cart.add(Cart.java:42)\n"
                                            "\tat com.shop.Api.handle(Api.java:7)"));
    QCOMPARE(records[2].message, QStringLiteral("next"));
    QCOMPARE(records[3].message, QStringLiteral("json\n  joined to json"));
    QCOMPARE(records[3].raw, QStringLiteral(R"({"level":"error","msg":"json"})"
                                            "\n  joined to json"));
}

void TestFileImporter::importsEmptyFiles() {
    const QString empty = writeFile(QStringLiteral("empty.log"), "");
    const QString bomOnly = writeFile(QStringLiteral("bom.log"), "\xEF\xBB\xBF");
    const QString blank = writeFile(QStringLiteral("blank.log"), "\n\r\n   \n\t\n");
    QVERIFY(!empty.isEmpty() && !bomOnly.isEmpty() && !blank.isEmpty());

    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{empty, {}}, {bomOnly, {}}, {blank, {}}});
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->added, 0);
    QVERIFY(!summary->cancelled);
    QCOMPARE(storedRecords(databasePath()).size(), 0);
}

void TestFileImporter::countsUnparsedLines() {
    const QString path = writeFile(QStringLiteral("mixed.log"),
                                   "INFO one\nnot a log line\nWARN two\n{broken json\nERROR three\n");
    QVERIFY(!path.isEmpty());
    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{path, QStringLiteral("m")}});
    QVERIFY(summary);
    QCOMPARE(summary->added, 5);
    QCOMPARE(summary->unparsed, 2);
    // Unparsed lines are stored too, marked for the user.
    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 5);
    QVERIFY(records[1].fields.contains(QStringLiteral("_parse_error")));
    QCOMPARE(records[3].message, QStringLiteral("{broken json"));
}

void TestFileImporter::namesSourcesPerFile() {
    const QString alpha = writeFile(QStringLiteral("alpha.log"), "INFO a1\nINFO a2\n");
    const QString beta = writeFile(QStringLiteral("beta.app.log"), "INFO b1\n"
                                                                   R"({"service":"payment","msg":"p1"})"
                                                                   "\n");
    QVERIFY(!alpha.isEmpty() && !beta.isEmpty());
    FileImporter importer;
    // A blank source falls back to the file name without its extension; a JSON service overrides both.
    const std::optional<ImportSummary> summary = runImport(
        importer, databasePath(), {{alpha, QStringLiteral("  custom  ")}, {beta, QStringLiteral(" ")}});
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));

    const QHash<QString, int> counts = countBySource(storedRecords(databasePath()));
    QCOMPARE(counts.size(), 3);
    QCOMPARE(counts.value(QStringLiteral("custom")), 2);
    QCOMPARE(counts.value(QStringLiteral("beta.app")), 1);
    QCOMPARE(counts.value(QStringLiteral("payment")), 1);
}

void TestFileImporter::limitsLongLinesAndRecords() {
    QByteArray content = "INFO " + QByteArray(100'000, 'x') + "\nERROR trace start\n";
    // About 90 KB of stack frames: more than one record may hold.
    for (int i = 0; i < 2000; ++i) {
        content += "    at frame " + QByteArray::number(i) + " of a very deep stack trace\n";
    }
    content += "INFO after\n";
    const QString path = writeFile(QStringLiteral("long.log"), content);
    QVERIFY(!path.isEmpty());

    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{path, QStringLiteral("l")}});
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->added, 3);

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 3);
    QCOMPARE(records[0].level, Level::Info);
    QCOMPARE(records[0].raw.size(), LineSplitter::kMaxLineBytes);
    QVERIFY(records[0].fields.contains(QStringLiteral(R"("_truncated":true)")));

    QCOMPARE(records[1].level, Level::Error);
    QVERIFY(records[1].message.startsWith(QStringLiteral("trace start\n    at frame 0 of")));
    QVERIFY(records[1].raw.toUtf8().size() <= LineSplitter::kMaxLineBytes);
    QVERIFY(records[1].raw.toUtf8().size() > LineSplitter::kMaxLineBytes - 100);
    QVERIFY(records[1].fields.contains(QStringLiteral(R"("_truncated":true)")));

    QCOMPARE(records[2].message, QStringLiteral("after"));
}

void TestFileImporter::reportsMissingFileAndKeepsEarlierLines() {
    const QString good = writeFile(QStringLiteral("good.log"), "INFO kept 1\nINFO kept 2\n");
    const QString missing = m_dir->filePath(QStringLiteral("missing.log"));
    const QString never = writeFile(QStringLiteral("never.log"), "INFO never read\n");
    QVERIFY(!good.isEmpty() && !never.isEmpty());

    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{good, {}}, {missing, {}}, {never, {}}});
    QVERIFY(summary);
    QVERIFY2(
        summary->error.startsWith(QStringLiteral("Cannot open %1").arg(QDir::toNativeSeparators(missing))),
        qPrintable(summary->error));
    QVERIFY(!summary->cancelled);
    // The import stops at the failed file, but what was read before it is written.
    QCOMPARE(summary->added, 2);
    const QHash<QString, int> counts = countBySource(storedRecords(databasePath()));
    QCOMPARE(counts.size(), 1);
    QCOMPARE(counts.value(QStringLiteral("good")), 2);
}

void TestFileImporter::rejectsUtf16File() {
    // "INFO x" in UTF-16 LE with its byte order mark, as Windows PowerShell 5 writes files.
    const QString path =
        writeFile(QStringLiteral("utf16.log"), QByteArray("\xFF\xFEI\0N\0F\0O\0 \0x\0\n\0", 16));
    QVERIFY(!path.isEmpty());
    FileImporter importer;
    const std::optional<ImportSummary> summary = runImport(importer, databasePath(), {{path, {}}});
    QVERIFY(summary);
    QVERIFY2(summary->error.contains(QStringLiteral("UTF-16")), qPrintable(summary->error));
    QCOMPARE(summary->added, 0);
}

void TestFileImporter::reportsDatabaseFailure() {
    const QString path = writeFile(QStringLiteral("app.log"), "INFO x\n");
    QVERIFY(!path.isEmpty());
    FileImporter importer;
    const std::optional<ImportSummary> summary =
        runImport(importer, m_dir->filePath(QStringLiteral("missing/dir/import.lvdb")), {{path, {}}});
    QVERIFY(summary);
    QVERIFY(!summary->error.isEmpty());
    QCOMPARE(summary->added, 0);
}

void TestFileImporter::refusesSecondStartWhileRunning() {
    const QString path = writeFile(QStringLiteral("app.log"), "INFO one\nINFO two\n");
    QVERIFY(!path.isEmpty());
    FileImporter importer;
    QSignalSpy finished(&importer, &FileImporter::finished);
    QVERIFY(importer.start(databasePath(), {{path, QStringLiteral("a")}}));
    QVERIFY(importer.isRunning());
    QVERIFY(!importer.start(databasePath(), {{path, QStringLiteral("b")}}));
    QVERIFY(finished.wait(kWaitMs));
    QVERIFY(!importer.isRunning());
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().first().value<ImportSummary>().added, 2);

    // Once finished, the importer can run again.
    QVERIFY(importer.start(databasePath(), {{path, QStringLiteral("b")}}));
    QVERIFY(finished.wait(kWaitMs));
    const QHash<QString, int> counts = countBySource(storedRecords(databasePath()));
    QCOMPARE(counts.value(QStringLiteral("a")), 2);
    QCOMPARE(counts.value(QStringLiteral("b")), 2);
}

void TestFileImporter::cancelKeepsCommittedTransactions() {
    constexpr int kLines = 300'000;
    QByteArray content;
    content.reserve(kLines * 20);
    for (int i = 0; i < kLines; ++i) {
        content += "INFO line " + QByteArray::number(i) + '\n';
    }
    const QString path = writeFile(QStringLiteral("big.log"), content);
    QVERIFY(!path.isEmpty());

    // A connection of the test thread watches the import (WAL lets it read while the import writes).
    Database reader;
    QVERIFY2(reader.open(databasePath()), qPrintable(reader.errorString()));
    LogStore store(reader);

    FileImporter importer;
    QSignalSpy finished(&importer, &FileImporter::finished);
    QVERIFY(importer.start(databasePath(), {{path, QStringLiteral("big")}}));
    QTRY_VERIFY_WITH_TIMEOUT(store.recordCount().value_or(0) > 0, kWaitMs);
    importer.cancel();
    QVERIFY(finished.wait(kWaitMs));

    const ImportSummary summary = finished.first().first().value<ImportSummary>();
    QVERIFY(summary.cancelled);
    QVERIFY2(summary.error.isEmpty(), qPrintable(summary.error));
    QVERIFY2(summary.added > 0 && summary.added < kLines, qPrintable(QString::number(summary.added)));
    // Only whole transactions were committed, and the summary counts exactly those.
    QCOMPARE(summary.added % FileImporter::kBatchSize, 0);
    QCOMPARE(store.recordCount().value_or(-1), summary.added);
}

void TestFileImporter::importsHundredThousandLines() {
    constexpr int kLines = 100'000;
    // JSON and plain lines in the formats of DESIGN 5.4, one every half second.
    QByteArray content;
    content.reserve(kLines * 90);
    for (int i = 0; i < kLines; ++i) {
        const int msecs = i * 500;
        const QByteArray clock = QString::asprintf("%02d:%02d:%02d.%03d", msecs / 3'600'000,
                                                   msecs / 60'000 % 60, msecs / 1000 % 60, msecs % 1000)
                                     .toLatin1();
        const QByteArray n = QByteArray::number(i);
        switch (i % 4) {
        case 0:
            content += R"({"time":"2026-11-19T)" + clock + R"(Z","level":"info","msg":"Request )" + n +
                       R"( done","service":"api","http":{"status":200},"user_id":)" + n + "}\n";
            break;
        case 1:
            content += "2026-11-19 " + clock + " WARN Slow query " + n + " took 250 ms\n";
            break;
        case 2:
            content += "[2026-11-19T" + clock + "+03:00] [ERROR] Payment " + n + " failed\n";
            break;
        default:
            content += "2026-11-19 " + clock + " DEBUG Cache hit for key user:" + n + "\n";
            break;
        }
    }
    const QString path = writeFile(QStringLiteral("big.log"), content);
    QVERIFY(!path.isEmpty());

    FileImporter importer;
    QObject context;
    QVector<int> percents;
    QString progressFile;
    connect(&importer, &FileImporter::progressChanged, &context, [&](int percent, const QString& fileName) {
        percents.append(percent);
        progressFile = fileName;
    });
    const std::optional<ImportSummary> summary =
        runImport(importer, databasePath(), {{path, QStringLiteral("big")}});
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->added, kLines);
    QCOMPARE(summary->unparsed, 0);
    qInfo("Imported %d lines (%lld KB) in %lld ms: %lld lines/s", kLines, qint64(content.size() / 1024),
          summary->elapsedMs, kLines * 1000LL / qMax<qint64>(1, summary->elapsedMs));
    // DESIGN 1.4 asks for 20,000 lines/s; a debug build under test load is held to a looser bound.
    QVERIFY2(summary->elapsedMs < 30'000, qPrintable(QString::number(summary->elapsedMs)));

    // Progress rises within 0-100, at most every kProgressIntervalMs.
    QVERIFY(!percents.isEmpty());
    QVERIFY(std::is_sorted(percents.cbegin(), percents.cend()));
    QVERIFY(percents.first() >= 0 && percents.last() <= 100);
    QVERIFY2(percents.size() <= summary->elapsedMs / FileImporter::kProgressIntervalMs + 1,
             qPrintable(QStringLiteral("%1 updates in %2 ms").arg(percents.size()).arg(summary->elapsedMs)));
    QCOMPARE(progressFile, QStringLiteral("big.log"));

    Database database;
    QVERIFY2(database.open(databasePath()), qPrintable(database.errorString()));
    LogStore store(database);
    QCOMPARE(store.recordCount().value_or(-1), kLines);
}
