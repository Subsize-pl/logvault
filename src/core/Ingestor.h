#pragma once

#include "LineParser.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class Database;
class LogStore;
class QTimer;

/// Collects received lines and writes them to the database in batches (DESIGN.md section 5.5): a batch of
/// up to kBatchSize lines is parsed and written in one transaction when it fills up, or kFlushDelayMs after
/// its first line. Owns its own database connection, so it must be used in one thread only: create it, move
/// it to the intake thread, and call every method there (start() first).
class Ingestor : public QObject {
    Q_OBJECT

public:
    static constexpr int kBatchSize = 500;
    static constexpr int kFlushDelayMs = 200;

    explicit Ingestor(QObject* parent = nullptr);
    /// Calls stop(). Runs in the intake thread when destroyed through deleteLater().
    ~Ingestor() override;

    /// Opens the database for the calling thread, stopping a previous session first. Returns false and
    /// emits errorOccurred() if it cannot be opened.
    bool start(const QString& databasePath);

    /// Queues lines for writing.
    void addLines(const QVector<ReceivedLine>& lines);

    /// Writes the pending lines at once and closes the database.
    void stop();

signals:
    /// A batch of count records was committed.
    void recordsWritten(int count);
    /// The database could not be opened, or a batch could not be written: its lines are lost.
    void errorOccurred(const QString& message);

private:
    void flush();
    void writeBatch(const QVector<ReceivedLine>& lines);

    std::unique_ptr<Database> m_database;
    std::unique_ptr<LogStore> m_store; // declared after m_database, so it is destroyed first
    QVector<ReceivedLine> m_pending;
    QTimer* m_flushTimer;
};
