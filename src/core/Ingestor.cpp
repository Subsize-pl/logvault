#include "Ingestor.h"

#include "Database.h"
#include "LogStore.h"

#include <QThread>
#include <QTimer>

#include <utility>

Ingestor::Ingestor(QObject* parent) : QObject(parent), m_flushTimer(new QTimer(this)) {
    // A child timer moves to the intake thread together with the ingestor.
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kFlushDelayMs);
    connect(m_flushTimer, &QTimer::timeout, this, &Ingestor::flush);
}

Ingestor::~Ingestor() { stop(); }

bool Ingestor::start(const QString& databasePath) {
    // The connection belongs to the thread that opens it and must be used only there.
    Q_ASSERT(QThread::currentThread() == thread());
    stop();
    auto database = std::make_unique<Database>();
    if (!database->open(databasePath)) {
        emit errorOccurred(database->errorString());
        return false;
    }
    m_database = std::move(database);
    m_store = std::make_unique<LogStore>(*m_database);
    return true;
}

void Ingestor::addLines(const QVector<ReceivedLine>& lines) {
    if (lines.isEmpty()) {
        return;
    }
    m_pending += lines;
    if (m_pending.size() >= kBatchSize) {
        flush();
    } else if (!m_flushTimer->isActive()) {
        // The delay counts from the first line of a batch, so a slow trickle is still written promptly.
        m_flushTimer->start();
    }
}

void Ingestor::stop() {
    flush();
    m_store.reset();
    m_database.reset();
}

void Ingestor::flush() {
    m_flushTimer->stop();
    const QVector<ReceivedLine> lines = std::exchange(m_pending, {});
    // Keeps transactions short even when one read delivered thousands of lines.
    for (qsizetype first = 0; first < lines.size(); first += kBatchSize) {
        writeBatch(lines.mid(first, kBatchSize));
    }
}

void Ingestor::writeBatch(const QVector<ReceivedLine>& lines) {
    if (!m_store) {
        emit errorOccurred(tr("Received lines were not saved: no database is open."));
        return;
    }
    QVector<Record> records;
    records.reserve(lines.size());
    for (const ReceivedLine& line : lines) {
        records.append(LineParser::parse(line));
    }
    if (!m_store->insertBatch(records)) {
        emit errorOccurred(tr("Received lines were not saved. %1").arg(m_store->errorString()));
        return;
    }
    emit recordsWritten(static_cast<int>(records.size()));
}
