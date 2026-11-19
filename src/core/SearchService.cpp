#include "SearchService.h"

#include "Database.h"
#include "LogStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QMetaType>

#include <limits>
#include <optional>
#include <utility>

namespace {

// A stale search stops after its current query, so the newest one never waits long for a thread, and
// old searches cannot take every core and slow the newest one down.
constexpr int kMaxParallelSearches = 2;

// One search. It runs entirely in a pool thread and opens and closes its database connection there.
class SearchJob {
    Q_DECLARE_TR_FUNCTIONS(SearchService)

public:
    SearchJob(SearchRequest request, quint64 generation, const std::atomic<quint64>& latest)
        : m_request(std::move(request)), m_latest(latest) {
        m_result.generation = generation;
    }

    // The result, or nullopt when a newer search started meanwhile: its result would be dropped anyway.
    std::optional<SearchResult> run(const QString& databasePath, const QElapsedTimer& timer);

private:
    bool isStale() const { return m_latest.load() != m_result.generation; }
    QString checkRequest(const QString& databasePath) const;
    void searchIn(const QString& databasePath);
    void query(LogStore& store);
    void fail(const QString& message);

    const SearchRequest m_request;
    const std::atomic<quint64>& m_latest;
    SearchResult m_result;
};

std::optional<SearchResult> SearchJob::run(const QString& databasePath, const QElapsedTimer& timer) {
    if (isStale()) {
        return std::nullopt;
    }
    if (const QString problem = checkRequest(databasePath); !problem.isEmpty()) {
        fail(problem);
    } else {
        searchIn(databasePath);
    }
    if (isStale()) {
        return std::nullopt;
    }
    m_result.elapsedMs = timer.elapsed();
    return m_result;
}

// Problems found without opening the database.
QString SearchJob::checkRequest(const QString& databasePath) const {
    if (databasePath.isEmpty()) {
        return tr("No database is open.");
    }
    // Opening a missing file would create an empty database, and a search never writes.
    if (!QFileInfo::exists(databasePath)) {
        return tr("Cannot search %1: the file does not exist.").arg(QDir::toNativeSeparators(databasePath));
    }
    if (m_request.statsMode == StatsMode::Field && !SearchQuery::isValidFieldName(m_request.statsField)) {
        return tr("Cannot compute statistics: \"%1\" is not a valid field name.").arg(m_request.statsField);
    }
    return {};
}

void SearchJob::searchIn(const QString& databasePath) {
    Database database;
    if (!database.open(databasePath)) {
        fail(database.errorString());
        return;
    }
    LogStore store(database); // destroyed before the database, as it must be
    if (!store.beginRead()) {
        fail(store.errorString());
        return;
    }
    // One read transaction: the total, the rows, the histogram and the statistics all describe the same
    // records, even while intake keeps writing.
    query(store);
    store.endRead();
}

// The queries of DESIGN.md section 6.3. Each one can take a while on a big database, so a search that has
// become stale stops between them.
void SearchJob::query(LogStore& store) {
    const SqlFragment condition = SearchQuery::build(m_request);
    const std::optional<MatchSpan> span = store.matchSpan(condition);
    if (!span) {
        fail(store.errorString());
        return;
    }
    if (!span->found) {
        // Nothing to read. A bounded period still gets its (empty) bars, so the time axis stays.
        if (m_request.from != 0 && m_request.to != 0) {
            m_result.histogram = Histogram::forRange(m_request.from, m_request.to - 1);
        }
        return;
    }

    if (isStale()) {
        return;
    }
    // An unbounded side of the period ("All time") ends at the oldest or newest match. Every match falls
    // into exactly one bar, so the bars also give the total without another pass over the records.
    const qint64 first = m_request.from != 0 ? m_request.from : span->firstTs;
    const qint64 last = m_request.to != 0 ? m_request.to - 1 : span->lastTs;
    std::optional<Histogram> histogram = store.histogram(condition, first, last);
    if (!histogram) {
        fail(store.errorString());
        return;
    }
    m_result.histogram = std::move(*histogram);
    m_result.total = m_result.histogram.total();

    if (isStale()) {
        return;
    }
    // Searching only between the oldest and the newest match stops the walk through the time index at the
    // oldest match instead of the oldest record, which matters when there are fewer matches than maxRows.
    SearchRequest within = m_request;
    within.from = span->firstTs;
    within.to = span->lastTs < std::numeric_limits<qint64>::max() ? span->lastTs + 1 : 0;
    std::optional<QVector<Record>> rows = store.findRecords(SearchQuery::build(within), m_request.maxRows);
    if (!rows) {
        fail(store.errorString());
        return;
    }
    m_result.rows = std::move(*rows);

    if (m_request.statsMode == StatsMode::None || isStale()) {
        return;
    }
    std::optional<QVector<StatsRow>> stats =
        m_request.statsMode == StatsMode::Field
            ? store.fieldStatistics(condition, m_request.statsField, m_result.total)
            : store.statistics(condition, m_request.statsMode);
    if (!stats) {
        fail(store.errorString());
        return;
    }
    m_result.stats = std::move(*stats);
}

void SearchJob::fail(const QString& message) {
    // A failed search carries only its number and the error.
    const quint64 generation = m_result.generation;
    m_result = SearchResult();
    m_result.generation = generation;
    m_result.error = message;
}

} // namespace

SearchService::SearchService(QObject* parent) : QObject(parent) {
    // Registered by name too, so these types can travel through any queued connection and QVariant.
    qRegisterMetaType<SearchResult>();
    qRegisterMetaType<Histogram>();
    qRegisterMetaType<StatsRow>();
    m_pool.setMaxThreadCount(kMaxParallelSearches);
}

SearchService::~SearchService() {
    // The running searches become stale, so they stop after their current query.
    ++m_generation;
    m_pool.waitForDone();
}

void SearchService::setDatabasePath(const QString& path) {
    m_databasePath = path;
    ++m_generation;
}

QString SearchService::databasePath() const { return m_databasePath; }

quint64 SearchService::search(const SearchRequest& request) {
    QElapsedTimer timer;
    timer.start();
    const quint64 generation = ++m_generation;
    m_pool.start([this, request, generation, timer, path = m_databasePath] {
        SearchJob job(request, generation, m_generation);
        std::optional<SearchResult> result = job.run(path, timer);
        if (result) {
            // deliver() runs in the GUI thread. The destructor waits for this task, so "this" is alive here;
            // a call still queued when the object is destroyed is discarded with it.
            QMetaObject::invokeMethod(
                this, [this, found = std::move(*result)] { deliver(found); }, Qt::QueuedConnection);
        }
    });
    return generation;
}

void SearchService::waitForFinished() { m_pool.waitForDone(); }

void SearchService::deliver(const SearchResult& result) {
    // A newer search may have started while this one was waiting in the event queue.
    if (result.generation == m_generation.load()) {
        emit finished(result);
    }
}
