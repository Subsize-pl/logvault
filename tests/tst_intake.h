#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

/// Checks the TCP intake: the Ingestor's batches, then TcpServer and Ingestor together in their own thread
/// with real client sockets. Any Qt warning fails a test, so an unclean shutdown is caught.
class TestIntake : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void ingestorWritesFullBatchAtOnce();
    void ingestorWritesAfterDelay();
    void ingestorStopWritesPendingLines();
    void ingestorReportsOpenFailure();
    void ingestorReportsLinesWithoutDatabase();
    void ingestorReportsFailedBatch();

    void receivesWholeLines();
    void receivesLineSplitAcrossWrites();
    void receivesManyLinesInOneWrite();
    void receivesFromSeveralClients();
    void keepsLastLineOfDroppedConnection();
    void truncatesOversizedLine();
    void refusesConnectionsOverLimit();
    void reportsBusyPort();
    void stopPassesUnfinishedLines();
    void reportsStats();

private:
    QString databasePath() const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
