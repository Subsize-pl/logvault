#pragma once

#include "SearchQuery.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>

/// The file formats of an export (DESIGN.md section 6.7).
enum class ExportFormat { Csv, JsonLines };

/// The outcome of an export.
struct ExportSummary {
    QString path;        // the file written
    qint64 exported = 0; // records written
    qint64 elapsedMs = 0;
    bool cancelled = false; // the file was not written
    QString error;          // empty on success; on an error the file was not written either
};

/// Writes every record that matches a search to a CSV or JSON Lines file, in the thread pool with a database
/// connection of its own (DESIGN.md section 6.7). The records are read and written one at a time, so any
/// number of them fits, and the file appears only when it is complete: a cancelled or failed export leaves
/// no partial file and keeps an existing file unchanged. Use the object from one thread; progressChanged()
/// comes from the pool thread.
class Exporter : public QObject {
    Q_OBJECT

public:
    static constexpr int kProgressIntervalMs = 100;

    explicit Exporter(QObject* parent = nullptr);
    /// Cancels a running export and waits for it to end.
    ~Exporter() override;

    /// Starts writing all records that match request (its maxRows and statistics are ignored) to the file at
    /// path. separator is the CSV field separator. Returns false while an export is running.
    bool start(const QString& databasePath, const SearchRequest& request, const QString& path,
               ExportFormat format, QChar separator = u',');

    /// Asks the running export to stop; the file is not written. Thread-safe.
    void cancel();

    bool isRunning() const;

    /// Blocks until the running export ends (when the database is closed).
    void waitForFinished();

    /// The list separator of the Windows regional settings, which Excel expects between the fields of a CSV
    /// file it opens; ',' when it is not set or cannot be used.
    static QChar listSeparator();

signals:
    /// Percent of the records written so far; at most every kProgressIntervalMs. Emitted from the pool
    /// thread.
    void progressChanged(int percent);
    void finished(const ExportSummary& summary);

private:
    QFutureWatcher<ExportSummary> m_watcher;
    std::atomic<bool> m_cancel = false;
    bool m_running = false;
};
