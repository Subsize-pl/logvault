#pragma once

#include "LogStore.h"
#include "Record.h"
#include "SearchQuery.h"

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <atomic>

/// The outcome of a search (DESIGN.md section 6.1).
struct SearchResult {
    quint64 generation = 0;      // the number search() returned
    QVector<Record> rows;        // at most maxRows, newest first
    qint64 total = 0;            // all matches
    Histogram histogram;         // no bars when nothing matches and the period is unbounded
    QVector<StatsRow> stats;     // only when statsMode is not None; largest groups first
    QVector<SourceInfo> sources; // every source and its record count (matches or not), by name
    qint64 elapsedMs = 0;
    QString error; // empty on success; otherwise the members above are empty
};

/// Runs searches in a thread pool, each with a database connection of its own, and delivers the result of
/// the latest one through finished(); results of older searches are dropped (DESIGN.md section 3.3).
/// Lives in the GUI thread; call it from that thread only.
class SearchService : public QObject {
    Q_OBJECT

public:
    explicit SearchService(QObject* parent = nullptr);
    /// Waits for the running searches, which stop after their current query.
    ~SearchService() override;

    /// The database file that searches read from now on. Results of searches started before are dropped.
    void setDatabasePath(const QString& path);
    QString databasePath() const;

    /// Starts a search and returns its generation number, which grows with every search. Its result, also
    /// a failed one, comes through finished() unless a newer search or setDatabasePath() came first.
    quint64 search(const SearchRequest& request);

    /// Blocks until no search is running. Computed results still arrive through the event loop.
    void waitForFinished();

signals:
    void finished(const SearchResult& result);

private:
    void deliver(const SearchResult& result);

    QString m_databasePath;
    // The number of the latest search; searches running in the pool read it to stop once they are stale.
    std::atomic<quint64> m_generation = 0;
    QThreadPool m_pool;
};
