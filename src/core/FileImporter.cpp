#include "FileImporter.h"

#include "Database.h"
#include "LineParser.h"
#include "LineSplitter.h"
#include "LogStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrentRun>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace {

constexpr qint64 kReadBlockSize = 256 * 1024;
constexpr QByteArrayView kUtf8Bom("\xEF\xBB\xBF");
constexpr QByteArrayView kUtf16LittleEndianBom("\xFF\xFE");
constexpr QByteArrayView kUtf16BigEndianBom("\xFE\xFF");

using ProgressCallback = std::function<void(int percent, const QString& fileName)>;

// The import itself. It runs entirely in the thread that calls run(), and everything it holds, the database
// connection included, belongs to that thread.
class ImportJob {
    Q_DECLARE_TR_FUNCTIONS(FileImporter)

public:
    ImportJob(const std::atomic<bool>& cancel, ProgressCallback progress)
        : m_cancel(cancel), m_progress(std::move(progress)) {}

    ImportSummary run(const QString& databasePath, const QVector<ImportFile>& files);

private:
    // A record still being read: its first line and the indented lines joined to it.
    struct PendingRecord {
        QByteArray firstLine;
        QByteArray continuation; // every joined line, each after a '\n'
        qint64 receivedAt = 0;
        bool truncated = false;
    };

    bool importFile(const ImportFile& file);
    bool addLine(const LineSplitter::Line& line, qint64 receivedAt);
    bool completeRecord();
    bool writeBatch();
    bool checkCancelled();
    bool fail(const QString& message);
    void reportProgress(qint64 bytesRead, const QString& fileName);

    const std::atomic<bool>& m_cancel;
    ProgressCallback m_progress;
    Database m_database;
    std::unique_ptr<LogStore> m_store; // declared after m_database, so it is destroyed first
    QString m_source;                  // of the file being read
    std::optional<PendingRecord> m_pending;
    QVector<Record> m_batch;
    qint64 m_batchUnparsed = 0;
    bool m_storeFailed = false;
    ImportSummary m_summary;
    qint64 m_totalBytes = 0;
    qint64 m_finishedBytes = 0; // bytes of the files already read
    int m_lastPercent = -1;
    QElapsedTimer m_progressClock;
};

ImportSummary ImportJob::run(const QString& databasePath, const QVector<ImportFile>& files) {
    QElapsedTimer timer;
    timer.start();
    if (!m_database.open(databasePath)) {
        fail(m_database.errorString());
    } else {
        m_store = std::make_unique<LogStore>(m_database);
        for (const ImportFile& file : files) {
            m_totalBytes += QFileInfo(file.path).size();
        }
        for (const ImportFile& file : files) {
            if (!importFile(file)) {
                break;
            }
        }
        // Lines read before a file failed are still written; after a cancel or a failed transaction, nothing.
        if (!m_summary.cancelled && !m_storeFailed && completeRecord()) {
            writeBatch();
        }
    }
    m_summary.elapsedMs = timer.elapsed();
    return m_summary;
}

bool ImportJob::importFile(const ImportFile& file) {
    const QString nativePath = QDir::toNativeSeparators(file.path);
    const QString fileName = QFileInfo(file.path).fileName();
    QFile input(file.path);
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(tr("Cannot open %1: %2").arg(nativePath, input.errorString()));
    }
    m_source = file.source.trimmed();
    if (m_source.isEmpty()) {
        m_source = QFileInfo(file.path).completeBaseName();
    }

    LineSplitter splitter;
    QByteArray buffer(kReadBlockSize, Qt::Uninitialized);
    bool firstBlock = true;
    while (!checkCancelled()) {
        const qint64 size = input.read(buffer.data(), buffer.size());
        if (size < 0) {
            return fail(tr("Cannot read %1: %2").arg(nativePath, input.errorString()));
        }
        if (size == 0) {
            break;
        }
        QByteArrayView block(buffer.constData(), size);
        if (firstBlock) {
            firstBlock = false;
            if (block.startsWith(kUtf16LittleEndianBom) || block.startsWith(kUtf16BigEndianBom)) {
                return fail(tr("%1 is a UTF-16 file; only UTF-8 files can be imported. Save it as UTF-8 "
                               "and import it again.")
                                .arg(nativePath));
            }
            if (block.startsWith(kUtf8Bom)) {
                block = block.sliced(kUtf8Bom.size());
            }
        }
        const qint64 receivedAt = QDateTime::currentMSecsSinceEpoch();
        for (const LineSplitter::Line& line : splitter.append(block)) {
            if (!addLine(line, receivedAt)) {
                return false;
            }
        }
        reportProgress(m_finishedBytes + input.pos(), fileName);
    }
    if (m_summary.cancelled) {
        return false;
    }
    if (const std::optional<LineSplitter::Line> last = splitter.finish()) {
        if (!addLine(*last, QDateTime::currentMSecsSinceEpoch())) {
            return false;
        }
    }
    m_finishedBytes += input.pos();
    // The last record of a file never continues into the next file.
    return completeRecord();
}

bool ImportJob::addLine(const LineSplitter::Line& line, qint64 receivedAt) {
    // An indented line continues the record above it: this joins stack traces.
    const bool indented = line.text.startsWith(' ') || line.text.startsWith('\t');
    if (indented && m_pending) {
        PendingRecord& record = *m_pending;
        const qsizetype joinedSize =
            record.firstLine.size() + record.continuation.size() + 1 + line.text.size();
        // A record is limited like a line, so a file of indented lines cannot make it grow without bounds.
        if (joinedSize <= LineSplitter::kMaxLineBytes) {
            record.continuation += '\n';
            record.continuation += line.text;
        } else {
            record.truncated = true;
        }
        return true;
    }
    if (!completeRecord()) {
        return false;
    }
    m_pending = PendingRecord{line.text, {}, receivedAt, line.truncated};
    return true;
}

bool ImportJob::completeRecord() {
    if (!m_pending) {
        return true;
    }
    const PendingRecord pending = std::move(*m_pending);
    m_pending.reset();

    bool parsed = true;
    Record record =
        LineParser::parse({pending.firstLine, m_source, pending.receivedAt, pending.truncated}, &parsed);
    if (!pending.continuation.isEmpty()) {
        const QString joined = QString::fromUtf8(pending.continuation);
        record.message += joined;
        record.raw += joined;
    }
    m_batch.append(std::move(record));
    if (!parsed) {
        ++m_batchUnparsed;
    }
    return m_batch.size() < FileImporter::kBatchSize || writeBatch();
}

bool ImportJob::writeBatch() {
    // A cancelled import keeps the committed transactions and drops the batch being collected.
    if (checkCancelled()) {
        return false;
    }
    if (m_batch.isEmpty()) {
        return true;
    }
    if (!m_store->insertBatch(m_batch)) {
        m_storeFailed = true;
        return fail(m_store->errorString());
    }
    m_summary.added += m_batch.size();
    m_summary.unparsed += m_batchUnparsed;
    m_batch.clear();
    m_batchUnparsed = 0;
    return true;
}

bool ImportJob::checkCancelled() {
    if (m_cancel.load()) {
        m_summary.cancelled = true;
    }
    return m_summary.cancelled;
}

bool ImportJob::fail(const QString& message) {
    // A second error (the final write after a file failed) must not hide the first one.
    m_summary.error += m_summary.error.isEmpty() ? message : u'\n' + message;
    return false;
}

void ImportJob::reportProgress(qint64 bytesRead, const QString& fileName) {
    if (m_totalBytes <= 0) {
        return;
    }
    // A file can grow while it is read, so the share is capped.
    const int percent = static_cast<int>(qMin<qint64>(bytesRead * 100 / m_totalBytes, 100));
    if (percent == m_lastPercent ||
        (m_progressClock.isValid() && m_progressClock.elapsed() < FileImporter::kProgressIntervalMs)) {
        return;
    }
    m_lastPercent = percent;
    m_progressClock.start();
    m_progress(percent, fileName);
}

} // namespace

FileImporter::FileImporter(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<ImportSummary>::finished, this, [this] {
        m_running = false;
        emit finished(m_watcher.result());
    });
}

FileImporter::~FileImporter() {
    // The job uses this object's flag and signals, so it must end before the object does.
    cancel();
    waitForFinished();
}

bool FileImporter::start(const QString& databasePath, const QVector<ImportFile>& files) {
    if (m_running) {
        return false;
    }
    m_running = true;
    m_cancel = false;
    m_watcher.setFuture(QtConcurrent::run([this, databasePath, files] {
        ImportJob job(m_cancel, [this](int percent, const QString& fileName) {
            emit progressChanged(percent, fileName);
        });
        return job.run(databasePath, files);
    }));
    return true;
}

void FileImporter::cancel() { m_cancel = true; }

bool FileImporter::isRunning() const { return m_running; }

void FileImporter::waitForFinished() { m_watcher.waitForFinished(); }
