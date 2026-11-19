#include "tst_intake.h"

#include "Database.h"
#include "Ingestor.h"
#include "IntakeRig.h"
#include "LineSplitter.h"
#include "StoredRecords.h"
#include "TcpServer.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <vector>

namespace {

const QString kTcpSource = QStringLiteral("tcp");
constexpr int kWaitMs = 10'000;

// A client connected from the test thread; null if it cannot connect.
std::unique_ptr<QTcpSocket> connectClient(quint16 port) {
    auto socket = std::make_unique<QTcpSocket>();
    socket->connectToHost(QHostAddress::LocalHost, port);
    if (!socket->waitForConnected(kWaitMs)) {
        return nullptr;
    }
    // Every write leaves at once, so pieces of a line really arrive separately.
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    return socket;
}

// Writes data and waits until the socket has handed all of it to the system.
bool sendAll(QTcpSocket& socket, const QByteArray& data) {
    if (socket.write(data) != data.size()) {
        return false;
    }
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(kWaitMs)) {
            return false;
        }
    }
    return true;
}

// Closes the connection the way a client program does when it exits.
bool disconnectClient(QTcpSocket& socket) {
    socket.disconnectFromHost();
    return socket.state() == QAbstractSocket::UnconnectedState || socket.waitForDisconnected(kWaitMs);
}

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

void TestIntake::receivesWholeLines() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.ingestorStarted);
    QVERIFY(rig.listen());
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);
    QVERIFY(sendAll(*client, "2026-11-19 14:05:01 INFO first\n"));
    QVERIFY(sendAll(*client, R"({"level":"error","msg":"second","service":"api"})"
                             "\n"));
    QVERIFY(sendAll(*client, "third, with nothing to recognize\r\n"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, 3, kWaitMs);
    rig.shutdown();

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 3);
    QCOMPARE(records[0].level, Level::Info);
    QCOMPARE(records[0].message, QStringLiteral("first"));
    QCOMPARE(records[0].source, kTcpSource);
    QCOMPARE(records[1].level, Level::Error);
    QCOMPARE(records[1].message, QStringLiteral("second"));
    QCOMPARE(records[1].source, QStringLiteral("api"));
    QCOMPARE(records[2].raw, QStringLiteral("third, with nothing to recognize"));
    QVERIFY(records[2].fields.contains(QStringLiteral("_parse_error")));
    QVERIFY2(rig.errors.isEmpty(), qPrintable(rig.errors.join(u'\n')));
}

void TestIntake::receivesLineSplitAcrossWrites() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);

    // Byte by byte...
    const QByteArray first = R"({"level":"warn","msg":"Hello, byte by byte"})"
                             "\n";
    for (const char ch : first) {
        QVERIFY(sendAll(*client, QByteArray(1, ch)));
    }
    // ...and in parts with pauses, so the server surely holds an unfinished line, even its '\r'.
    QVERIFY(sendAll(*client, "2026-11-19 14:05:01 ERROR second "));
    QTest::qWait(100);
    QVERIFY(sendAll(*client, "half\r"));
    QTest::qWait(100);
    QVERIFY(sendAll(*client, "\n"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, 2, kWaitMs);
    rig.shutdown();

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 2);
    QCOMPARE(records[0].level, Level::Warn);
    QCOMPARE(records[0].message, QStringLiteral("Hello, byte by byte"));
    QCOMPARE(records[1].level, Level::Error);
    QCOMPARE(records[1].message, QStringLiteral("second half"));
    QCOMPARE(records[1].raw, QStringLiteral("2026-11-19 14:05:01 ERROR second half"));
}

void TestIntake::receivesManyLinesInOneWrite() {
    constexpr int kLines = 2000;
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);

    QByteArray data;
    for (int i = 0; i < kLines; ++i) {
        data += "INFO line " + QByteArray::number(i) + '\n';
    }
    QVERIFY(sendAll(*client, data));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, kLines, kWaitMs);
    rig.shutdown();

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), kLines);
    for (int i = 0; i < kLines; ++i) {
        QCOMPARE(records[i].message, QStringLiteral("line %1").arg(i));
    }
}

void TestIntake::receivesFromSeveralClients() {
    constexpr int kClients = 5;
    constexpr int kLines = 200;
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    std::vector<std::unique_ptr<QTcpSocket>> clients;
    for (int c = 0; c < kClients; ++c) {
        clients.push_back(connectClient(rig.port()));
        QVERIFY(clients.back());
    }

    // The clients take turns, so their lines interleave on the server.
    for (int i = 0; i < kLines; ++i) {
        for (int c = 0; c < kClients; ++c) {
            const QString line = QStringLiteral(R"({"service":"client-%1","msg":"line %2"})").arg(c).arg(i);
            QVERIFY(sendAll(*clients[c], line.toUtf8() + '\n'));
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, kClients * kLines, kWaitMs);
    rig.shutdown();

    // Each client's lines are all there, in the order that client sent them.
    QHash<QString, int> nextLine;
    for (const Record& record : storedRecords(databasePath())) {
        QCOMPARE(record.message, QStringLiteral("line %1").arg(nextLine[record.source]++));
    }
    QCOMPARE(nextLine.size(), kClients);
    for (const int count : std::as_const(nextLine)) {
        QCOMPARE(count, kLines);
    }
}

void TestIntake::keepsLastLineOfDroppedConnection() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);
    QVERIFY(sendAll(*client, "INFO complete\nWARN cut off in the mid"));
    QVERIFY(disconnectClient(*client));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, 2, kWaitMs);
    rig.shutdown();

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 2);
    QCOMPARE(records[0].message, QStringLiteral("complete"));
    QCOMPARE(records[1].level, Level::Warn);
    QCOMPARE(records[1].message, QStringLiteral("cut off in the mid"));
}

void TestIntake::truncatesOversizedLine() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);

    // A slow client: the long line arrives in many pieces.
    const QByteArray huge(200'000, 'x');
    for (qsizetype pos = 0; pos < huge.size(); pos += 10'000) {
        QVERIFY(sendAll(*client, huge.mid(pos, 10'000)));
    }
    QVERIFY(sendAll(*client, "\nINFO after the long line\n"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, 2, kWaitMs);
    rig.shutdown();

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 2);
    QCOMPARE(records[0].raw.size(), LineSplitter::kMaxLineBytes);
    QVERIFY2(records[0].fields.contains(QStringLiteral(R"("_truncated":true)")),
             qPrintable(records[0].fields));
    QCOMPARE(records[1].message, QStringLiteral("after the long line"));
}

void TestIntake::refusesConnectionsOverLimit() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    std::vector<std::unique_ptr<QTcpSocket>> clients;
    for (int i = 0; i < TcpServer::kMaxConnections; ++i) {
        clients.push_back(connectClient(rig.port()));
        QVERIFY(clients.back());
    }
    // The server takes connections asynchronously; its stats show when all of them are in.
    QTRY_COMPARE_WITH_TIMEOUT(rig.stats.connections.size(), TcpServer::kMaxConnections, kWaitMs);

    // The system completes the handshake, then the server closes the connection at once.
    const std::unique_ptr<QTcpSocket> extra = connectClient(rig.port());
    QVERIFY(extra);
    QVERIFY(extra->state() == QAbstractSocket::UnconnectedState || extra->waitForDisconnected(kWaitMs));

    // The accepted clients keep working.
    QVERIFY(sendAll(*clients.back(), "INFO still here\n"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, 1, kWaitMs);
    QCOMPARE(rig.stats.connections.size(), TcpServer::kMaxConnections);
}

void TestIntake::reportsBusyPort() {
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
    IntakeRig rig(databasePath());

    QVERIFY(!rig.listen(blocker.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.errors.isEmpty(), kWaitMs);
    const QString expected = QStringLiteral("Cannot listen on 127.0.0.1:%1").arg(blocker.serverPort());
    QVERIFY2(rig.errors.first().startsWith(expected), qPrintable(rig.errors.first()));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.stats.error.isEmpty(), kWaitMs);
    QVERIFY(!rig.stats.listening);

    // The receiver can still start on a free port.
    QVERIFY(rig.listen());
    QTRY_VERIFY_WITH_TIMEOUT(rig.stats.listening, kWaitMs);
    QVERIFY(rig.stats.error.isEmpty());
}

void TestIntake::stopPassesUnfinishedLines() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);
    // Once the complete line is written, the unfinished one sent with it is surely in the server's buffer.
    QVERIFY(sendAll(*client, "INFO complete\nINFO unfinished at shutdown"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, 1, kWaitMs);
    rig.shutdown();

    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), 2);
    QCOMPARE(records[1].message, QStringLiteral("unfinished at shutdown"));
}

void TestIntake::reportsStats() {
    IntakeRig rig(databasePath());
    QVERIFY(rig.listen());
    QTRY_VERIFY_WITH_TIMEOUT(rig.stats.listening, kWaitMs);
    QCOMPARE(rig.stats.address, QStringLiteral("127.0.0.1"));
    QVERIFY(rig.port() != 0);
    QCOMPARE(rig.stats.port, rig.port());

    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    const std::unique_ptr<QTcpSocket> client = connectClient(rig.port());
    QVERIFY(client);
    QVERIFY(sendAll(*client, "INFO one\nINFO two\nINFO three\n"));
    QTRY_VERIFY_WITH_TIMEOUT(rig.stats.connections.size() == 1 && rig.stats.connections.first().lines == 3,
                             kWaitMs);
    const ConnectionInfo info = rig.stats.connections.first();
    QVERIFY(info.id != 0);
    QVERIFY2(info.address.startsWith(QStringLiteral("127.0.0.1:")), qPrintable(info.address));
    QVERIFY(info.connectedAt >= before && info.connectedAt <= QDateTime::currentMSecsSinceEpoch());
    QCOMPARE(rig.stats.totalLines, 3);
    // The rate covers the second in which the lines arrived.
    QTRY_VERIFY_WITH_TIMEOUT(rig.maxLinesPerSecond >= 1, kWaitMs);
    QVERIFY(rig.maxLinesPerSecond <= 3);

    // A closed connection leaves the list; the total stays.
    QVERIFY(disconnectClient(*client));
    QTRY_VERIFY_WITH_TIMEOUT(rig.stats.connections.isEmpty(), kWaitMs);
    QCOMPARE(rig.stats.totalLines, 3);

    // stop() reports the stopped state at once.
    rig.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.stats.listening, kWaitMs);
}
