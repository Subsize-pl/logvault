#include "tst_intake.h"

#include "Database.h"
#include "Ingestor.h"
#include "StoredRecords.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

namespace {

const QString kTcpSource = QStringLiteral("tcp");

// JSON lines "line <first>" ... with consecutive numbers.
QVector<ReceivedLine> makeLines(int count, int first = 0) {
    QVector<ReceivedLine> lines;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = first; i < first + count; ++i) {
        lines.append(
            {R"({"level":"info","msg":"line )" + QByteArray::number(i) + R"("})", kTcpSource, now, false});
    }
    return lines;
}

} // namespace

void TestIntake::init() {
    // A warning means something was not shut down cleanly (a running thread, a connection still in use).
    QTest::failOnWarning();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestIntake::cleanup() { m_dir.reset(); }

QString TestIntake::databasePath() const { return m_dir->filePath(QStringLiteral("intake.lvdb")); }

void TestIntake::ingestorWritesFullBatchAtOnce() {
    Ingestor ingestor;
    QSignalSpy written(&ingestor, &Ingestor::recordsWritten);
    QVERIFY(ingestor.start(databasePath()));

    ingestor.addLines(makeLines(Ingestor::kBatchSize - 1));
    QCOMPARE(written.count(), 0);
    ingestor.addLines(makeLines(1, Ingestor::kBatchSize - 1));
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(0).toInt(), Ingestor::kBatchSize);

    // A large delivery is written in transactions of at most kBatchSize records.
    ingestor.addLines(makeLines(Ingestor::kBatchSize * 2 + 10, Ingestor::kBatchSize));
    QCOMPARE(written.count(), 4);
    QCOMPARE(written.at(1).at(0).toInt(), Ingestor::kBatchSize);
    QCOMPARE(written.at(2).at(0).toInt(), Ingestor::kBatchSize);
    QCOMPARE(written.at(3).at(0).toInt(), 10);
    ingestor.stop();

    QString error;
    const QVector<Record> records = storedRecords(databasePath(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(records.size(), Ingestor::kBatchSize * 3 + 10);
    for (qsizetype i = 0; i < records.size(); ++i) {
        QCOMPARE(records[i].message, QStringLiteral("line %1").arg(i));
    }
    QCOMPARE(records.first().level, Level::Info);
    QCOMPARE(records.first().source, kTcpSource);
}

void TestIntake::ingestorWritesAfterDelay() {
    Ingestor ingestor;
    QSignalSpy written(&ingestor, &Ingestor::recordsWritten);
    QVERIFY(ingestor.start(databasePath()));

    QElapsedTimer timer;
    timer.start();
    ingestor.addLines(makeLines(3));
    QCOMPARE(written.count(), 0);
    QVERIFY(written.wait(5000));
    // A coarse timer may fire a few percent early.
    QVERIFY2(timer.elapsed() >= Ingestor::kFlushDelayMs * 9 / 10,
             qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(written.at(0).at(0).toInt(), 3);

    // The delay counts from the first line of a batch: later lines join it without restarting the timer.
    timer.restart();
    ingestor.addLines(makeLines(2, 3));
    QTest::qWait(Ingestor::kFlushDelayMs / 2);
    QCOMPARE(written.count(), 1);
    ingestor.addLines(makeLines(2, 5));
    QVERIFY(written.wait(5000));
    QCOMPARE(written.at(1).at(0).toInt(), 4);
    QVERIFY2(timer.elapsed() < Ingestor::kFlushDelayMs * 3, qPrintable(QString::number(timer.elapsed())));
    ingestor.stop();
    QCOMPARE(storedRecords(databasePath()).size(), 7);
}

void TestIntake::ingestorStopWritesPendingLines() {
    Ingestor ingestor;
    QSignalSpy written(&ingestor, &Ingestor::recordsWritten);
    QVERIFY(ingestor.start(databasePath()));
    ingestor.addLines(makeLines(7));
    QCOMPARE(written.count(), 0);

    ingestor.stop();
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(0).toInt(), 7);
    QCOMPARE(storedRecords(databasePath()).size(), 7);

    // stop() closed the database; start() opens it again.
    QVERIFY(ingestor.start(databasePath()));
    ingestor.addLines(makeLines(2, 7));
    ingestor.stop();
    QCOMPARE(storedRecords(databasePath()).size(), 9);

    // The destructor writes what is pending as well.
    {
        Ingestor scoped;
        QVERIFY(scoped.start(databasePath()));
        scoped.addLines(makeLines(1, 9));
    }
    QCOMPARE(storedRecords(databasePath()).size(), 10);
}

void TestIntake::ingestorReportsOpenFailure() {
    Ingestor ingestor;
    QSignalSpy errors(&ingestor, &Ingestor::errorOccurred);
    QVERIFY(!ingestor.start(m_dir->filePath(QStringLiteral("missing/dir/intake.lvdb"))));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!errors.at(0).at(0).toString().isEmpty());
}

void TestIntake::ingestorReportsLinesWithoutDatabase() {
    Ingestor ingestor;
    QSignalSpy written(&ingestor, &Ingestor::recordsWritten);
    QSignalSpy errors(&ingestor, &Ingestor::errorOccurred);
    ingestor.addLines(makeLines(3));
    ingestor.stop();
    QCOMPARE(written.count(), 0);
    QCOMPARE(errors.count(), 1);
    QVERIFY2(errors.at(0).at(0).toString().contains(QStringLiteral("not saved")),
             qPrintable(errors.at(0).at(0).toString()));
}

void TestIntake::ingestorReportsFailedBatch() {
    const QString path = databasePath();
    {
        // A test-only trigger makes one record of a batch fail.
        Database database;
        QVERIFY2(database.open(path), qPrintable(database.errorString()));
        QSqlQuery query(database.connection());
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TRIGGER poison BEFORE INSERT ON records "
                     "WHEN NEW.message = 'poison' BEGIN SELECT RAISE(ABORT, 'poisoned'); END")),
                 qPrintable(query.lastError().text()));
    }

    Ingestor ingestor;
    QSignalSpy written(&ingestor, &Ingestor::recordsWritten);
    QSignalSpy errors(&ingestor, &Ingestor::errorOccurred);
    QVERIFY(ingestor.start(path));
    QVector<ReceivedLine> lines = makeLines(3);
    lines.append({"ERROR poison", kTcpSource, QDateTime::currentMSecsSinceEpoch(), false});
    ingestor.addLines(lines);
    ingestor.stop();

    // The whole batch is rolled back and reported; nothing is written silently in part.
    QCOMPARE(written.count(), 0);
    QCOMPARE(errors.count(), 1);
    QVERIFY2(errors.at(0).at(0).toString().contains(QStringLiteral("poisoned")),
             qPrintable(errors.at(0).at(0).toString()));
    QCOMPARE(storedRecords(path).size(), 0);

    // The next batch is written normally.
    QVERIFY(ingestor.start(path));
    ingestor.addLines(makeLines(2));
    ingestor.stop();
    QCOMPARE(written.count(), 1);
    QCOMPARE(storedRecords(path).size(), 2);
}
