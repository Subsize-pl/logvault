#include "tst_pythonclient.h"

#include "FileImporter.h"
#include "IntakeRig.h"
#include "StoredRecords.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace {

constexpr int kWaitMs = 60'000;

// The first "python" or "python3" in PATH that really is Python 3, or an empty string. On Windows "python"
// may be the Microsoft Store stub, which only prints a hint.
QString findPython() {
    for (const QString& name : {QStringLiteral("python"), QStringLiteral("python3")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (path.isEmpty()) {
            continue;
        }
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.start(path, {QStringLiteral("--version")});
        if (probe.waitForFinished(kWaitMs) && probe.exitStatus() == QProcess::NormalExit &&
            probe.exitCode() == 0 && probe.readAll().startsWith("Python 3")) {
            return path;
        }
    }
    return {};
}

} // namespace

void TestPythonClient::initTestCase() {
    m_python = findPython();
    if (m_python.isEmpty()) {
        QSKIP("Python 3 was not found in PATH");
    }
    m_script = QString::fromUtf8(LV_SOURCE_DIR) + QStringLiteral("/examples/python_client.py");
    QVERIFY2(QFileInfo::exists(m_script), qPrintable(m_script));
}

void TestPythonClient::init() {
    // A warning means something was not shut down cleanly (a running thread, a connection still in use).
    QTest::failOnWarning();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestPythonClient::cleanup() { m_dir.reset(); }

QString TestPythonClient::databasePath() const { return m_dir->filePath(QStringLiteral("python.lvdb")); }

bool TestPythonClient::runClient(const QStringList& arguments, QString& output) const {
    QProcess client;
    client.setProcessChannelMode(QProcess::MergedChannels);
    client.start(m_python, QStringList{m_script} + arguments);
    const bool finished = client.waitForFinished(kWaitMs);
    output = QString::fromLocal8Bit(client.readAll());
    if (!finished) {
        output += client.errorString();
        client.kill();
        client.waitForFinished();
        return false;
    }
    return client.exitStatus() == QProcess::NormalExit && client.exitCode() == 0;
}

void TestPythonClient::countLinesAllArrive() {
    constexpr int kLines = 10'000;
    IntakeRig rig(databasePath());
    QVERIFY(rig.ingestorStarted);
    QVERIFY(rig.listen());

    QElapsedTimer timer;
    timer.start();
    QString output;
    QVERIFY2(runClient({QStringLiteral("--count"), QString::number(kLines), QStringLiteral("--port"),
                        QString::number(rig.port())},
                       output),
             qPrintable(output));
    QTRY_COMPARE_WITH_TIMEOUT(rig.written, kLines, kWaitMs);
    qInfo("Python client: %d lines sent and stored in %lld ms", kLines, timer.elapsed());
    rig.shutdown();
    QVERIFY2(rig.errors.isEmpty(), qPrintable(rig.errors.join(u'\n')));

    // Every line exactly once and in order: nothing lost, nothing doubled.
    const QVector<Record> records = storedRecords(databasePath());
    QCOMPARE(records.size(), kLines);
    for (int i = 0; i < kLines; ++i) {
        QCOMPARE(records[i].message, QStringLiteral("Test line %1 of %2").arg(i + 1).arg(kLines));
    }
    QCOMPARE(records.first().level, Level::Info);
    QCOMPARE(records.first().source, QStringLiteral("loadtest"));
    QVERIFY2(records.first().fields.contains(QStringLiteral(R"("seq":1)")),
             qPrintable(records.first().fields));
}

void TestPythonClient::demoFileImportsCleanly() {
    // A minute of the shop at 50 lines per second, so the payment burst (from 20 s on) is in it.
    const QString file = m_dir->filePath(QStringLiteral("shop.log"));
    QString output;
    QVERIFY2(
        runClient({QStringLiteral("--demo"), QStringLiteral("--file"), file, QStringLiteral("--duration"),
                   QStringLiteral("60"), QStringLiteral("--rate"), QStringLiteral("50")},
                  output),
        qPrintable(output));

    FileImporter importer;
    QSignalSpy finished(&importer, &FileImporter::finished);
    QVERIFY(importer.start(databasePath(), {{file, QStringLiteral("shop")}}));
    QVERIFY(finished.wait(kWaitMs));
    const ImportSummary summary = finished.first().first().value<ImportSummary>();
    QVERIFY2(summary.error.isEmpty(), qPrintable(summary.error));
    QCOMPARE(summary.added, 3000);
    QCOMPARE(summary.unparsed, 0);

    QSet<QString> sources;
    QSet<int> levels;
    int timeouts = 0;
    for (const Record& record : storedRecords(databasePath())) {
        sources.insert(record.source);
        levels.insert(static_cast<int>(record.level));
        if (record.message.startsWith(QStringLiteral("Payment provider timeout after "))) {
            ++timeouts;
            QVERIFY2(record.fields.contains(QStringLiteral(R"("provider":"bepaid")")),
                     qPrintable(record.fields));
        }
        if (record.source == QStringLiteral("gateway")) {
            QVERIFY2(record.fields.contains(QStringLiteral(R"("http.status":)")), qPrintable(record.fields));
        }
    }
    QCOMPARE(sources, (QSet<QString>{QStringLiteral("gateway"), QStringLiteral("auth"),
                                     QStringLiteral("cart"), QStringLiteral("payment")}));
    QVERIFY2(timeouts > 100, qPrintable(QString::number(timeouts)));
    for (const Level level : {Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error}) {
        QVERIFY2(levels.contains(static_cast<int>(level)), qPrintable(levelName(level)));
    }
    QVERIFY(!levels.contains(static_cast<int>(Level::Unknown)));
}
