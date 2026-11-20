#include "Exporter.h"

#include "Database.h"
#include "LogStore.h"
#include "Record.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QTimeZone>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace {

// The text is written to the file in blocks of about this many characters: fewer, larger writes.
constexpr qsizetype kWriteBlockSize = 256 * 1024;
// The cancel flag and the progress are checked once per this many records.
constexpr qint64 kCheckInterval = 1000;

constexpr QChar kByteOrderMark(0xFEFF);

using ProgressCallback = std::function<void(int percent)>;

// RFC 4180: a field that holds the separator, a quote or a line break goes in quotes, with its quotes
// doubled.
void appendCsvField(QString& out, const QString& text, QChar separator) {
    const bool quoted = std::any_of(text.begin(), text.end(), [separator](QChar ch) {
        return ch == separator || ch == u'"' || ch == u'\n' || ch == u'\r';
    });
    if (!quoted) {
        out += text;
        return;
    }
    out += u'"';
    for (const QChar ch : text) {
        if (ch == u'"') {
            out += u'"';
        }
        out += ch;
    }
    out += u'"';
}

// The time as the records table shows it, in the display zone: Excel reads this form as a date and time.
QString csvTime(qint64 msecs, const QTimeZone& zone) {
    return QDateTime::fromMSecsSinceEpoch(msecs, zone).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

void appendCsvRecord(QString& out, const Record& record, QChar separator, const QTimeZone& zone) {
    const std::array<QString, 5> fields = {csvTime(record.ts, zone), levelName(record.level), record.source,
                                           record.message, record.fields};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            out += separator;
        }
        appendCsvField(out, fields[i], separator);
    }
    // RFC 4180 ends every line with CRLF, line breaks inside quoted fields stay as they are.
    out += u"\r\n";
}

// A JSON string (RFC 8259): quotes, backslashes and control characters escaped.
void appendJsonString(QString& out, const QString& text) {
    out += u'"';
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case u'"':
            out += u"\\\"";
            break;
        case u'\\':
            out += u"\\\\";
            break;
        case u'\n':
            out += u"\\n";
            break;
        case u'\r':
            out += u"\\r";
            break;
        case u'\t':
            out += u"\\t";
            break;
        default:
            if (ch.unicode() < 0x20) {
                out += QStringLiteral("\\u%1").arg(static_cast<int>(ch.unicode()), 4, 16, QChar(u'0'));
            } else {
                out += ch;
            }
        }
    }
    out += u'"';
}

// The fields as a compact JSON object, {} when there are none. Text that is not a JSON object (only a
// database edited by hand has such) stays a string, so nothing is lost and the line is still valid JSON.
void appendJsonFields(QString& out, const QString& fields) {
    if (fields.isEmpty()) {
        out += u"{}";
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(fields.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
        out += QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    } else {
        appendJsonString(out, fields);
    }
}

// The time in UTC with milliseconds, e.g. 2026-11-20T11:05:01.123Z: the form LogVault reads back on import.
QString isoTime(qint64 msecs) {
    return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

// One record per line, the keys in the order a reader expects them.
void appendJsonRecord(QString& out, const Record& record) {
    out += u"{\"time\":";
    appendJsonString(out, isoTime(record.ts));
    out += u",\"level\":";
    appendJsonString(out, levelName(record.level));
    out += u",\"source\":";
    appendJsonString(out, record.source);
    out += u",\"message\":";
    appendJsonString(out, record.message);
    out += u",\"fields\":";
    appendJsonFields(out, record.fields);
    out += u"}\n";
}

// One export. It runs entirely in the thread that calls run(), and everything it holds, the database
// connection included, belongs to that thread.
class ExportJob {
    Q_DECLARE_TR_FUNCTIONS(Exporter)

public:
    ExportJob(ExportFormat format, QChar separator, const QTimeZone& zone, const std::atomic<bool>& cancel,
              ProgressCallback progress)
        : m_format(format), m_separator(separator), m_zone(zone), m_cancel(cancel),
          m_progress(std::move(progress)) {}

    ExportSummary run(const QString& databasePath, const SearchRequest& request, const QString& path);

private:
    void exportRecords(const QString& databasePath, const SearchRequest& request, const QString& path);
    bool writeRecords(LogStore& store, const SqlFragment& condition, QSaveFile& file);
    void appendHeader();
    bool flush(QSaveFile& file);
    bool checkCancelled();
    void reportProgress();
    bool fail(const QString& message);

    const ExportFormat m_format;
    const QChar m_separator;
    const QTimeZone m_zone;
    const std::atomic<bool>& m_cancel;
    ProgressCallback m_progress;
    QString m_text; // not written to the file yet
    qint64 m_total = 0;
    int m_lastPercent = -1;
    QElapsedTimer m_progressClock;
    ExportSummary m_summary;
};

ExportSummary ExportJob::run(const QString& databasePath, const SearchRequest& request, const QString& path) {
    QElapsedTimer timer;
    timer.start();
    m_summary.path = path;
    exportRecords(databasePath, request, path);
    m_summary.elapsedMs = timer.elapsed();
    return m_summary;
}

void ExportJob::exportRecords(const QString& databasePath, const SearchRequest& request,
                              const QString& path) {
    const QString nativePath = QDir::toNativeSeparators(path);
    // An export only reads: opening a missing file would create an empty database.
    if (!QFileInfo::exists(databasePath)) {
        fail(tr("Cannot export from %1: the file does not exist.")
                 .arg(QDir::toNativeSeparators(databasePath)));
        return;
    }
    if (QFileInfo(path) == QFileInfo(databasePath)) {
        fail(tr("Cannot export to %1: it is the database itself.").arg(nativePath));
        return;
    }
    Database database;
    if (!database.open(databasePath)) {
        fail(database.errorString());
        return;
    }
    LogStore store(database); // destroyed before the database, as it must be
    // The records go to a temporary file that replaces the target only when it is complete, so a cancelled
    // or failed export leaves no partial file and an existing file stays as it was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1: %2").arg(nativePath, file.errorString()));
        return;
    }
    if (!store.beginRead()) {
        fail(store.errorString());
        return;
    }
    // One read transaction: the count and the records describe the same state while intake writes on.
    const bool written = writeRecords(store, SearchQuery::build(request), file);
    store.endRead();
    if (!written) {
        // QSaveFile removes its temporary file when it is destroyed without commit().
        file.cancelWriting();
        return;
    }
    if (!file.commit()) {
        fail(tr("Cannot write %1: %2").arg(nativePath, file.errorString()));
    }
}

bool ExportJob::writeRecords(LogStore& store, const SqlFragment& condition, QSaveFile& file) {
    const std::optional<qint64> total = store.countRecords(condition);
    if (!total) {
        return fail(store.errorString());
    }
    m_total = *total;
    appendHeader();
    const bool read = store.visitRecords(condition, [this, &file](const Record& record) {
        if (m_format == ExportFormat::Csv) {
            appendCsvRecord(m_text, record, m_separator, m_zone);
        } else {
            appendJsonRecord(m_text, record);
        }
        ++m_summary.exported;
        if (m_text.size() >= kWriteBlockSize && !flush(file)) {
            return false;
        }
        if (m_summary.exported % kCheckInterval == 0) {
            if (checkCancelled()) {
                return false;
            }
            reportProgress();
        }
        return true;
    });
    if (!read) {
        return fail(store.errorString());
    }
    // A cancel that came after the last check still counts: the user does not want the file.
    if (!m_summary.error.isEmpty() || checkCancelled()) {
        return false;
    }
    return flush(file);
}

void ExportJob::appendHeader() {
    if (m_format != ExportFormat::Csv) {
        return;
    }
    // Excel takes a UTF-8 file without a byte order mark for one in the local code page.
    m_text += kByteOrderMark;
    // Times in UTC say so: the file may be read far from here.
    const std::array<QString, 5> names = {m_zone.timeSpec() == Qt::UTC ? QStringLiteral("Time (UTC)")
                                                                       : QStringLiteral("Time"),
                                          QStringLiteral("Level"), QStringLiteral("Source"),
                                          QStringLiteral("Message"), QStringLiteral("Fields")};
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            m_text += m_separator;
        }
        appendCsvField(m_text, names[i], m_separator);
    }
    m_text += u"\r\n";
}

bool ExportJob::flush(QSaveFile& file) {
    const QByteArray bytes = m_text.toUtf8();
    m_text.clear();
    if (file.write(bytes) != bytes.size()) {
        return fail(
            tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
    }
    return true;
}

bool ExportJob::checkCancelled() {
    if (m_cancel.load()) {
        m_summary.cancelled = true;
    }
    return m_summary.cancelled;
}

void ExportJob::reportProgress() {
    if (m_total <= 0) {
        return;
    }
    // Intake can add matches after the count, so the share is capped.
    const int percent = static_cast<int>(qMin<qint64>(m_summary.exported * 100 / m_total, 100));
    if (percent == m_lastPercent ||
        (m_progressClock.isValid() && m_progressClock.elapsed() < Exporter::kProgressIntervalMs)) {
        return;
    }
    m_lastPercent = percent;
    m_progressClock.start();
    m_progress(percent);
}

bool ExportJob::fail(const QString& message) {
    if (m_summary.error.isEmpty()) {
        m_summary.error = message;
    }
    return false;
}

} // namespace

Exporter::Exporter(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<ExportSummary>::finished, this, [this] {
        m_running = false;
        emit finished(m_watcher.result());
    });
}

Exporter::~Exporter() {
    // The job uses this object's flag and signals, so it must end before the object does.
    cancel();
    waitForFinished();
}

bool Exporter::start(const QString& databasePath, const SearchRequest& request, const QString& path,
                     ExportFormat format, QChar separator, const QTimeZone& zone) {
    if (m_running) {
        return false;
    }
    m_running = true;
    m_cancel = false;
    m_watcher.setFuture(QtConcurrent::run([this, databasePath, request, path, format, separator, zone] {
        ExportJob job(format, separator, zone, m_cancel,
                      [this](int percent) { emit progressChanged(percent); });
        return job.run(databasePath, request, path);
    }));
    return true;
}

void Exporter::cancel() { m_cancel = true; }

bool Exporter::isRunning() const { return m_running; }

void Exporter::waitForFinished() { m_watcher.waitForFinished(); }

QChar Exporter::listSeparator() {
    const QSettings international(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\International"),
                                  QSettings::NativeFormat);
    const QString separator = international.value(QStringLiteral("sList")).toString();
    if (separator.size() != 1) {
        return u',';
    }
    // A quote or a line break would make every field ambiguous.
    const QChar ch = separator.front();
    return ch == u'"' || ch == u'\r' || ch == u'\n' ? QChar(u',') : ch;
}
