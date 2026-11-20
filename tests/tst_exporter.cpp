#include "tst_exporter.h"

#include "Database.h"
#include "LogStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int kWaitMs = 60'000;
// 2026-11-20 10:00:00 UTC.
constexpr qint64 kT = 1'795'168'800'000;

const QByteArray kByteOrderMark("\xEF\xBB\xBF");

Record makeRecord(qint64 ts, Level level, const QString& source, const QString& message,
                  const QString& fields = {}) {
    Record record;
    record.ts = ts;
    record.level = level;
    record.source = source;
    record.message = message;
    record.fields = fields;
    record.raw = message;
    return record;
}

// Records whose CSV form needs every kind of quoting, oldest first.
QVector<Record> trickyRecords() {
    return {
        makeRecord(kT, Level::Info, u"web"_s, u"plain"_s),
        makeRecord(kT + 1, Level::Warn, u"a,b"_s, u"say \"hi\""_s, uR"({"k":"v,w"})"_s),
        makeRecord(kT + 2, Level::Error, u"s"_s, u"line1\nline2"_s),
        makeRecord(kT + 3, Level::Fatal, u"s"_s, u"cr\r\nlf"_s),
        makeRecord(kT + 4, Level::Unknown, u"semi;colon"_s, u"x"_s),
    };
}

QString localTime(qint64 msecs) {
    return QDateTime::fromMSecsSinceEpoch(msecs).toString(u"yyyy-MM-dd HH:mm:ss.zzz"_s);
}

// The lines of a CSV file, each ended by CRLF, in UTF-8.
QByteArray csvText(const QStringList& lines) { return (lines.join(u"\r\n"_s) + u"\r\n"_s).toUtf8(); }

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

void TestExporter::init() {
    // A warning means something was not shut down cleanly (a pool task, a connection still in use).
    QTest::failOnWarning();
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestExporter::cleanup() { m_dir.reset(); }

QString TestExporter::databasePath() const { return m_dir->filePath(u"export.lvdb"_s); }

QString TestExporter::filePath(const QString& name) const { return m_dir->filePath(name); }

bool TestExporter::fill(const QVector<Record>& records) const {
    Database database;
    if (!database.open(databasePath())) {
        return false;
    }
    LogStore store(database);
    return store.insertBatch(records);
}

std::optional<ExportSummary> TestExporter::exportTo(const QString& path, ExportFormat format, QChar separator,
                                                    const SearchRequest& request) const {
    Exporter exporter;
    QSignalSpy finished(&exporter, &Exporter::finished);
    if (!exporter.start(databasePath(), request, path, format, separator) || !finished.wait(kWaitMs)) {
        return std::nullopt;
    }
    return finished.takeFirst().first().value<ExportSummary>();
}

void TestExporter::csvQuotesFieldsThatNeedIt() {
    QVERIFY(fill(trickyRecords()));
    const QString path = filePath(u"out.csv"_s);
    const std::optional<ExportSummary> summary = exportTo(path, ExportFormat::Csv);
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QVERIFY(!summary->cancelled);
    QCOMPARE(summary->exported, 5);
    QCOMPARE(summary->path, path);

    // A byte order mark, a header and CRLF after every record, newest first. Fields with a comma, a quote
    // or a line break are quoted, quotes doubled; line breaks inside a field stay as they are.
    const QStringList lines = {
        u"Time,Level,Source,Message,Fields"_s,
        localTime(kT + 4) + u",UNKNOWN,semi;colon,x,"_s,
        localTime(kT + 3) + u",FATAL,s,\"cr\r\nlf\","_s,
        localTime(kT + 2) + u",ERROR,s,\"line1\nline2\","_s,
        localTime(kT + 1) + uR"(,WARN,"a,b","say ""hi""","{""k"":""v,w""}")"_s,
        localTime(kT) + u",INFO,web,plain,"_s,
    };
    QCOMPARE(readFile(path), kByteOrderMark + csvText(lines));
}

void TestExporter::csvUsesTheGivenSeparator() {
    QVERIFY(fill(trickyRecords()));
    const QString path = filePath(u"out.csv"_s);
    const std::optional<ExportSummary> summary = exportTo(path, ExportFormat::Csv, u';');
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));

    // Now the semicolon needs quotes and the comma does not.
    const QStringList lines = {
        u"Time;Level;Source;Message;Fields"_s,
        localTime(kT + 4) + u";UNKNOWN;\"semi;colon\";x;"_s,
        localTime(kT + 3) + u";FATAL;s;\"cr\r\nlf\";"_s,
        localTime(kT + 2) + u";ERROR;s;\"line1\nline2\";"_s,
        localTime(kT + 1) + uR"(;WARN;a,b;"say ""hi""";"{""k"":""v,w""}")"_s,
        localTime(kT) + u";INFO;web;plain;"_s,
    };
    QCOMPARE(readFile(path), kByteOrderMark + csvText(lines));
}

void TestExporter::jsonLinesParseBackToTheRecords() {
    const QString cyrillic = QString(QChar(0x0416)) + QChar(0x0443) + QChar(0x043A);
    const QString emoji = QString(QChar(0xD83D)) + QChar(0xDE00);
    const QString message =
        u"quote \" backslash \\ tab \t bell "_s + QChar(0x07) + u" new\nline "_s + cyrillic + u' ' + emoji;
    QVERIFY(fill({
        makeRecord(kT, Level::Info, cyrillic, message, uR"({"n":1,"b":true,"z":null,"s":"x\"y"})"_s),
        makeRecord(kT + 1500, Level::Error, u"web"_s, u"no fields"_s),
        makeRecord(kT + 3000, Level::Unknown, u"web"_s, u"broken fields"_s, u"not json"_s),
    }));
    const QString path = filePath(u"out.jsonl"_s);
    const std::optional<ExportSummary> summary = exportTo(path, ExportFormat::JsonLines);
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->exported, 3);

    // No byte order mark; one object per line, each line ending with LF.
    const QByteArray bytes = readFile(path);
    QVERIFY(!bytes.startsWith(kByteOrderMark));
    QVERIFY(bytes.endsWith('\n'));
    const QList<QByteArray> lines = bytes.chopped(1).split('\n');
    QCOMPARE(lines.size(), 3);
    QList<QJsonObject> objects;
    for (const QByteArray& line : lines) {
        // The keys come in reading order: time first.
        QVERIFY2(line.startsWith(R"({"time":)"), line.constData());
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(error.errorString()));
        QVERIFY(document.isObject());
        objects.append(document.object());
    }

    // Newest first, so the first record comes last.
    const QJsonObject first = objects.at(2);
    QCOMPARE(first.value(u"time"_s).toString(), u"2026-11-20T10:00:00.000Z"_s);
    QCOMPARE(first.value(u"level"_s).toString(), u"INFO"_s);
    QCOMPARE(first.value(u"source"_s).toString(), cyrillic);
    QCOMPARE(first.value(u"message"_s).toString(), message);
    const QJsonObject fields = first.value(u"fields"_s).toObject();
    QCOMPARE(fields.value(u"n"_s).toInt(), 1);
    QCOMPARE(fields.value(u"b"_s).toBool(), true);
    QVERIFY(fields.value(u"z"_s).isNull());
    QCOMPARE(fields.value(u"s"_s).toString(), u"x\"y"_s);

    QCOMPARE(objects.at(1).value(u"time"_s).toString(), u"2026-11-20T10:00:01.500Z"_s);
    QVERIFY(objects.at(1).value(u"fields"_s).isObject());
    QVERIFY(objects.at(1).value(u"fields"_s).toObject().isEmpty());
    // Fields that are not a JSON object are kept as text.
    QCOMPARE(objects.at(0).value(u"level"_s).toString(), u"UNKNOWN"_s);
    QCOMPARE(objects.at(0).value(u"fields"_s).toString(), u"not json"_s);
}

void TestExporter::exportsEveryMatchNewestFirst() {
    constexpr int kRecords = 10'000;
    QVector<Record> records;
    for (int i = 0; i < kRecords; ++i) {
        records.append(makeRecord(kT + i * 1000LL, i % 2 == 0 ? Level::Error : Level::Info, u"api"_s,
                                  u"Request %1"_s.arg(i)));
    }
    QVERIFY(fill(records));

    // The table's row limit and the statistics do not apply to an export.
    SearchRequest request;
    request.levels = {Level::Error};
    request.maxRows = 10;
    request.statsMode = StatsMode::Message;
    const QString path = filePath(u"errors.csv"_s);
    const std::optional<ExportSummary> summary = exportTo(path, ExportFormat::Csv, u',', request);
    QVERIFY(summary);
    QVERIFY2(summary->error.isEmpty(), qPrintable(summary->error));
    QCOMPARE(summary->exported, kRecords / 2);

    // The header, every match, and the empty piece after the last CRLF.
    const QList<QByteArray> lines = readFile(path).split('\n');
    QCOMPARE(lines.size(), 1 + kRecords / 2 + 1);
    QVERIFY(lines.last().isEmpty());
    for (qsizetype i = 1; i <= kRecords / 2; ++i) {
        const QByteArray expectedEnd = u",ERROR,api,Request %1,\r"_s.arg(kRecords - 2 * i).toUtf8();
        QVERIFY2(lines.at(i).endsWith(expectedEnd), lines.at(i).constData());
    }
}

void TestExporter::cancelLeavesNoFile() {
    constexpr int kRecords = 30'000;
    QVector<Record> records;
    for (int i = 0; i < kRecords; ++i) {
        records.append(makeRecord(kT + i, Level::Info, u"api"_s, u"Request %1"_s.arg(i)));
    }
    QVERIFY(fill(records));
    const QString path = filePath(u"big.csv"_s);

    // Cancelled at the first progress report, in the pool thread, while the export is under way.
    const auto cancelledExport = [this, &path] {
        Exporter exporter;
        QSignalSpy finished(&exporter, &Exporter::finished);
        connect(
            &exporter, &Exporter::progressChanged, &exporter, [&exporter] { exporter.cancel(); },
            Qt::DirectConnection);
        if (!exporter.start(databasePath(), {}, path, ExportFormat::Csv) || !finished.wait(kWaitMs)) {
            return ExportSummary();
        }
        return finished.takeFirst().first().value<ExportSummary>();
    };

    ExportSummary summary = cancelledExport();
    QVERIFY(summary.cancelled);
    QVERIFY2(summary.error.isEmpty(), qPrintable(summary.error));
    QVERIFY(summary.exported < kRecords);
    QVERIFY(!QFile::exists(path));

    // A file that was there before stays as it was.
    {
        QFile old(path);
        QVERIFY(old.open(QIODevice::WriteOnly));
        QVERIFY(old.write("old content") > 0);
    }
    summary = cancelledExport();
    QVERIFY(summary.cancelled);
    QCOMPARE(readFile(path), QByteArray("old content"));

    // No temporary file is left behind.
    QStringList others;
    for (const QString& name : QDir(m_dir->path()).entryList(QDir::Files | QDir::Hidden)) {
        if (!name.startsWith(u"export.lvdb"_s)) {
            others.append(name);
        }
    }
    QCOMPARE(others, QStringList{u"big.csv"_s});
}

void TestExporter::reportsErrorsWithoutWritingFiles() {
    // No database: neither the database nor the export file is created.
    std::optional<ExportSummary> summary = exportTo(filePath(u"a.csv"_s), ExportFormat::Csv);
    QVERIFY(summary);
    QVERIFY2(summary->error.contains(u"does not exist"_s), qPrintable(summary->error));
    QVERIFY(!QFile::exists(databasePath()));
    QVERIFY(!QFile::exists(filePath(u"a.csv"_s)));

    QVERIFY(fill({makeRecord(kT, Level::Info, u"web"_s, u"x"_s)}));
    summary = exportTo(filePath(u"no-such-folder/a.csv"_s), ExportFormat::Csv);
    QVERIFY(summary);
    QVERIFY2(summary->error.startsWith(u"Cannot write"_s), qPrintable(summary->error));

    // Writing over the database would destroy it.
    summary = exportTo(databasePath(), ExportFormat::JsonLines);
    QVERIFY(summary);
    QVERIFY2(summary->error.contains(u"the database itself"_s), qPrintable(summary->error));
    Database database;
    QVERIFY2(database.open(databasePath()), qPrintable(database.errorString()));
    LogStore store(database);
    QCOMPARE(store.recordCount().value_or(-1), 1);
}

void TestExporter::listSeparatorIsUsable() {
    const QChar separator = Exporter::listSeparator();
    QVERIFY(!separator.isNull());
    QVERIFY(separator != u'"' && separator != u'\r' && separator != u'\n');
}
