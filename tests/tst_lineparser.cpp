#include "tst_lineparser.h"

#include "LineParser.h"
#include "Record.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

namespace {

constexpr qint64 kReceivedAt = 1'700'000'000'000;
const QString kDefaultSource = QStringLiteral("tcp");

Record parseLine(const QByteArray& text, bool* ok = nullptr, bool truncated = false) {
    return LineParser::parse({text, kDefaultSource, kReceivedAt, truncated}, ok);
}

QJsonObject fieldsOf(const Record& record) {
    return QJsonDocument::fromJson(record.fields.toUtf8()).object();
}

qint64 utc(int year, int month, int day, int hour, int minute, int second, int msec = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, msec), QTimeZone::UTC)
        .toMSecsSinceEpoch();
}

qint64 local(int year, int month, int day, int hour, int minute, int second, int msec = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, msec), QTimeZone::LocalTime)
        .toMSecsSinceEpoch();
}

} // namespace

void TestLineParser::levelSynonyms_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<Level>("level");

    QTest::newRow("trace") << "trace" << Level::Trace;
    QTest::newRow("debug") << "Debug" << Level::Debug;
    QTest::newRow("dbg") << "DBG" << Level::Debug;
    QTest::newRow("info") << "INFO" << Level::Info;
    QTest::newRow("information") << "Information" << Level::Info;
    QTest::newRow("notice") << "notice" << Level::Info;
    QTest::newRow("warn") << "WARN" << Level::Warn;
    QTest::newRow("warning") << "Warning" << Level::Warn;
    QTest::newRow("error") << "error" << Level::Error;
    QTest::newRow("err") << "ERR" << Level::Error;
    QTest::newRow("fatal") << "FATAL" << Level::Fatal;
    QTest::newRow("critical") << "Critical" << Level::Fatal;
    QTest::newRow("crit") << "crit" << Level::Fatal;
    QTest::newRow("panic") << "PANIC" << Level::Fatal;
    QTest::newRow("verbose") << "verbose" << Level::Unknown;
    QTest::newRow("errors") << "errors" << Level::Unknown;
    QTest::newRow("empty") << "" << Level::Unknown;
}

void TestLineParser::levelSynonyms() {
    QFETCH(QString, name);
    QFETCH(Level, level);
    QCOMPARE(LineParser::levelFromName(name), level);
}

void TestLineParser::jsonSpecialFields() {
    bool ok = false;
    const QByteArray line = R"({"time":"2026-11-19T14:05:01.123Z","level":"error","msg":"Payment failed",)"
                            R"("service":"payment","provider":"bepaid","latency_ms":3012})";
    const Record record = parseLine(line, &ok);
    QVERIFY(ok);
    QCOMPARE(record.ts, utc(2026, 11, 19, 14, 5, 1, 123));
    QCOMPARE(record.level, Level::Error);
    QCOMPARE(record.message, QStringLiteral("Payment failed"));
    QCOMPARE(record.source, QStringLiteral("payment"));
    QCOMPARE(record.raw, QString::fromUtf8(line));
    QCOMPARE(record.id, 0);

    // Only the other fields remain, with their JSON types.
    const QJsonObject fields = fieldsOf(record);
    QCOMPARE(fields.size(), 2);
    QCOMPARE(fields.value(QStringLiteral("provider")).toString(), QStringLiteral("bepaid"));
    QCOMPARE(fields.value(QStringLiteral("latency_ms")).toInt(), 3012);
}

void TestLineParser::jsonFieldNames_data() {
    QTest::addColumn<QByteArray>("line");

    QTest::newRow("time level message source")
        << QByteArray(R"({"time":"2026-11-19T14:05:01Z","level":"warn","message":"m","source":"s"})");
    QTest::newRow("timestamp severity msg service")
        << QByteArray(R"({"timestamp":"2026-11-19T14:05:01Z","severity":"warn","msg":"m","service":"s"})");
    QTest::newRow("ts lvl msg app") << QByteArray(
        R"({"ts":"2026-11-19T14:05:01Z","lvl":"warn","msg":"m","app":"s"})");
    QTest::newRow("@timestamp") << QByteArray(
        R"({"@timestamp":"2026-11-19T14:05:01Z","level":"warn","msg":"m","app":"s"})");
    QTest::newRow("any letter case") << QByteArray(
        R"({"TimeStamp":"2026-11-19T14:05:01Z","LEVEL":"Warn","Msg":"m","Service":"s"})");
}

void TestLineParser::jsonFieldNames() {
    QFETCH(QByteArray, line);
    bool ok = false;
    const Record record = parseLine(line, &ok);
    QVERIFY(ok);
    QCOMPARE(record.ts, utc(2026, 11, 19, 14, 5, 1));
    QCOMPARE(record.level, Level::Warn);
    QCOMPARE(record.message, QStringLiteral("m"));
    QCOMPARE(record.source, QStringLiteral("s"));
    QVERIFY2(record.fields.isEmpty(), qPrintable(record.fields));
}

void TestLineParser::jsonFieldPriority() {
    // The first name in the list wins; the others stay as ordinary fields.
    const Record record = parseLine(R"({"ts":"2026-01-01T00:00:00Z","time":"2026-11-19T14:05:01Z",)"
                                    R"("lvl":"debug","level":"error","msg":"short","message":"long",)"
                                    R"("app":"a","service":"b","source":"c"})");
    QCOMPARE(record.ts, utc(2026, 11, 19, 14, 5, 1));
    QCOMPARE(record.level, Level::Error);
    QCOMPARE(record.message, QStringLiteral("long"));
    QCOMPARE(record.source, QStringLiteral("c"));

    const QJsonObject fields = fieldsOf(record);
    QCOMPARE(fields.size(), 5);
    QCOMPARE(fields.value(QStringLiteral("ts")).toString(), QStringLiteral("2026-01-01T00:00:00Z"));
    QCOMPARE(fields.value(QStringLiteral("lvl")).toString(), QStringLiteral("debug"));
    QCOMPARE(fields.value(QStringLiteral("msg")).toString(), QStringLiteral("short"));
    QCOMPARE(fields.value(QStringLiteral("app")).toString(), QStringLiteral("a"));
    QCOMPARE(fields.value(QStringLiteral("service")).toString(), QStringLiteral("b"));
}

void TestLineParser::jsonNestedObjectsAreFlattened() {
    const Record record =
        parseLine(R"({"msg":"m","http":{"status":500,"request":{"method":"GET"}},)"
                  R"("tags":["a",{"b":1}],"empty":{},"user":{"id":7,"admin":false},"note":null})");
    const QJsonObject fields = fieldsOf(record);
    QCOMPARE(fields.value(QStringLiteral("http.status")).toInt(), 500);
    QCOMPARE(fields.value(QStringLiteral("http.request.method")).toString(), QStringLiteral("GET"));
    QCOMPARE(fields.value(QStringLiteral("tags")).toString(), QStringLiteral(R"(["a",{"b":1}])"));
    QCOMPARE(fields.value(QStringLiteral("user.id")).toInt(), 7);
    QCOMPARE(fields.value(QStringLiteral("user.admin")).toBool(true), false);
    QVERIFY(fields.value(QStringLiteral("note")).isNull());
    QVERIFY(!fields.contains(QStringLiteral("http")));
    QVERIFY(!fields.contains(QStringLiteral("empty")));
    QCOMPARE(fields.size(), 6);
}

void TestLineParser::jsonUnusableValuesStayInFields() {
    // Special fields whose values cannot be used keep them as ordinary fields, so nothing is lost.
    bool ok = false;
    const Record record =
        parseLine(R"({"time":"yesterday","level":"verbose","msg":{"text":"hi"},"service":"  "})", &ok);
    QVERIFY(ok);
    QCOMPARE(record.ts, kReceivedAt);
    QCOMPARE(record.level, Level::Unknown);
    QCOMPARE(record.source, kDefaultSource);
    QCOMPARE(record.message, record.raw);

    const QJsonObject fields = fieldsOf(record);
    QCOMPARE(fields.value(QStringLiteral("time")).toString(), QStringLiteral("yesterday"));
    QCOMPARE(fields.value(QStringLiteral("level")).toString(), QStringLiteral("verbose"));
    QCOMPARE(fields.value(QStringLiteral("msg.text")).toString(), QStringLiteral("hi"));
    QCOMPARE(fields.value(QStringLiteral("service")).toString(), QStringLiteral("  "));
    QVERIFY(!fields.contains(QStringLiteral("_parse_error")));

    // Numeric levels have different meanings in different loggers, so they are not guessed.
    const Record numeric = parseLine(R"({"level":50,"time":42,"msg":"m"})");
    QCOMPARE(numeric.level, Level::Unknown);
    QCOMPARE(numeric.ts, kReceivedAt);
    QCOMPARE(fieldsOf(numeric).value(QStringLiteral("level")).toInt(), 50);
    QCOMPARE(fieldsOf(numeric).value(QStringLiteral("time")).toInt(), 42);
}

void TestLineParser::jsonNumbersAndScalars() {
    QCOMPARE(parseLine(R"({"time":1763560000,"msg":"m"})").ts, 1'763'560'000'000);
    QCOMPARE(parseLine(R"({"time":1763560000.5,"msg":"m"})").ts, 1'763'560'000'500);
    QCOMPARE(parseLine(R"({"ts":1763560000123,"msg":"m"})").ts, 1'763'560'000'123);
    QCOMPARE(parseLine(R"({"time":"1763560000","msg":"m"})").ts, 1'763'560'000'000);
    QCOMPARE(parseLine(R"({"time":"2026-11-19 14:05:01","msg":"m"})").ts, local(2026, 11, 19, 14, 5, 1));

    QCOMPARE(parseLine(R"({"msg":42})").message, QStringLiteral("42"));
    QCOMPARE(parseLine(R"({"msg":1.5})").message, QStringLiteral("1.5"));
    QCOMPARE(parseLine(R"({"msg":true})").message, QStringLiteral("true"));
    QCOMPARE(parseLine(R"({"msg":""})").message, QString());
    QCOMPARE(parseLine(R"({"msg":"m","service":7})").source, QStringLiteral("7"));
    QCOMPARE(parseLine(R"({"msg":"m","source":"  api  "})").source, QStringLiteral("api"));

    // A long id keeps every digit, which a double could not hold.
    const Record record = parseLine(R"({"msg":"m","order_id":12345678901234567})");
    QVERIFY2(record.fields.contains(QStringLiteral("12345678901234567")), qPrintable(record.fields));
}

void TestLineParser::jsonWithoutMessageShowsWholeLine() {
    bool ok = false;
    const QByteArray line = R"(  {"level":"info","code":42}  )";
    const Record record = parseLine(line, &ok);
    QVERIFY(ok);
    QCOMPARE(record.level, Level::Info);
    QCOMPARE(record.message, QStringLiteral(R"({"level":"info","code":42})"));
    QCOMPARE(record.raw, QString::fromUtf8(line));
    QCOMPARE(fieldsOf(record).value(QStringLiteral("code")).toInt(), 42);

    const Record empty = parseLine("{}", &ok);
    QVERIFY(ok);
    QCOMPARE(empty.message, QStringLiteral("{}"));
    QVERIFY(empty.fields.isEmpty());
}

void TestLineParser::plainText_data() {
    QTest::addColumn<QByteArray>("line");
    QTest::addColumn<qint64>("ts");
    QTest::addColumn<Level>("level");
    QTest::addColumn<QString>("message");

    QTest::newRow("python format") << QByteArray("2026-11-19 14:05:01,123 ERROR Payment failed")
                                   << local(2026, 11, 19, 14, 5, 1, 123) << Level::Error
                                   << QStringLiteral("Payment failed");
    QTest::newRow("ISO and [LEVEL]") << QByteArray("2026-11-19T14:05:01Z [WARN] Disk almost full")
                                     << utc(2026, 11, 19, 14, 5, 1) << Level::Warn
                                     << QStringLiteral("Disk almost full");
    QTest::newRow("both in brackets")
        << QByteArray("[2026-11-19 14:05:01] [error] Boom") << local(2026, 11, 19, 14, 5, 1) << Level::Error
        << QStringLiteral("Boom");
    QTest::newRow("LEVEL:") << QByteArray("[2026-11-19T14:05:01.5+03:00] INFO: started")
                            << utc(2026, 11, 19, 11, 5, 1, 500) << Level::Info << QStringLiteral("started");
    QTest::newRow("no level") << QByteArray("2026-11-19 14:05:01.250 Starting server on :8080")
                              << local(2026, 11, 19, 14, 5, 1, 250) << Level::Unknown
                              << QStringLiteral("Starting server on :8080");
    QTest::newRow("no time") << QByteArray("ERROR: disk full") << kReceivedAt << Level::Error
                             << QStringLiteral("disk full");
    QTest::newRow("padded bracket") << QByteArray("[INFO ] padded") << kReceivedAt << Level::Info
                                    << QStringLiteral("padded");
    QTest::newRow("extra spaces") << QByteArray("  2026-11-19T14:05:01Z   DEBUG   indented  ")
                                  << utc(2026, 11, 19, 14, 5, 1) << Level::Debug
                                  << QStringLiteral("indented");
    QTest::newRow("tab separated") << QByteArray("2026-11-19T14:05:01Z\tWARNING\ttabbed")
                                   << utc(2026, 11, 19, 14, 5, 1) << Level::Warn << QStringLiteral("tabbed");
    QTest::newRow("level only") << QByteArray("2026-11-19 14:05:01 critical") << local(2026, 11, 19, 14, 5, 1)
                                << Level::Fatal << QString();
    QTest::newRow("level word starts a sentence") << QByteArray("Information about the update") << kReceivedAt
                                                  << Level::Info << QStringLiteral("about the update");
    QTest::newRow("unknown word after time")
        << QByteArray("2026-11-19 14:05:01 main: ready") << local(2026, 11, 19, 14, 5, 1) << Level::Unknown
        << QStringLiteral("main: ready");
    QTest::newRow("bracketed non-level")
        << QByteArray("2026-11-19 14:05:01 [main] ready") << local(2026, 11, 19, 14, 5, 1) << Level::Unknown
        << QStringLiteral("[main] ready");
}

void TestLineParser::plainText() {
    QFETCH(QByteArray, line);
    QFETCH(qint64, ts);
    QFETCH(Level, level);
    QFETCH(QString, message);

    bool ok = false;
    const Record record = parseLine(line, &ok);
    QVERIFY(ok);
    QCOMPARE(record.ts, ts);
    QCOMPARE(record.level, level);
    QCOMPARE(record.message, message);
    QCOMPARE(record.source, kDefaultSource);
    QCOMPARE(record.raw, QString::fromUtf8(line));
    QVERIFY2(record.fields.isEmpty(), qPrintable(record.fields));
}

void TestLineParser::unrecognizedLines_data() {
    QTest::addColumn<QByteArray>("line");

    QTest::newRow("sentence") << QByteArray("Just a sentence without a time or a level");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("unterminated JSON") << QByteArray(R"({"level":"error","msg":"cut)");
    QTest::newRow("JSON with trailing text") << QByteArray(R"({"msg":"m"} trailing)");
    QTest::newRow("not JSON in braces") << QByteArray("{not json}");
    QTest::newRow("JSON array") << QByteArray(R"(["error","boom"])");
    QTest::newRow("time glued to text") << QByteArray("2026-11-19T14:05:01Zerror happened");
    QTest::newRow("level glued to text") << QByteArray("ERRORS were found");
    QTest::newRow("level with dash") << QByteArray("error-prone code");
    QTest::newRow("unclosed bracket") << QByteArray("[2026-11-19 14:05:01 ERROR x");
    QTest::newRow("number first") << QByteArray("12345 something happened");
}

void TestLineParser::unrecognizedLines() {
    QFETCH(QByteArray, line);
    bool ok = true;
    const Record record = parseLine(line, &ok);
    QVERIFY(!ok);
    QCOMPARE(record.ts, kReceivedAt);
    QCOMPARE(record.level, Level::Unknown);
    QCOMPARE(record.source, kDefaultSource);
    QCOMPARE(record.message, QString::fromUtf8(line).trimmed());
    QCOMPARE(record.raw, QString::fromUtf8(line));
    const QJsonObject fields = fieldsOf(record);
    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.value(QStringLiteral("_parse_error")).toBool(), true);
}

void TestLineParser::truncatedFlag() {
    const Record json = parseLine(R"({"msg":"m","k":"v"})", nullptr, true);
    QCOMPARE(fieldsOf(json).value(QStringLiteral("_truncated")).toBool(), true);
    QCOMPARE(fieldsOf(json).value(QStringLiteral("k")).toString(), QStringLiteral("v"));

    const Record text = parseLine("2026-11-19 14:05:01 INFO long line", nullptr, true);
    QCOMPARE(text.level, Level::Info);
    QCOMPARE(fieldsOf(text).value(QStringLiteral("_truncated")).toBool(), true);

    // A cut JSON line no longer parses: both flags are set.
    bool ok = true;
    const Record cut = parseLine(R"({"msg":"abc)", &ok, true);
    QVERIFY(!ok);
    const QJsonObject fields = fieldsOf(cut);
    QCOMPARE(fields.value(QStringLiteral("_parse_error")).toBool(), true);
    QCOMPARE(fields.value(QStringLiteral("_truncated")).toBool(), true);

    QVERIFY(!parseLine("INFO short").fields.contains(QStringLiteral("_truncated")));
}

void TestLineParser::rawKeepsOriginalText() {
    const QByteArray line = "  \t2026-11-19 14:05:01 WARN spaced out   ";
    const Record record = parseLine(line);
    QCOMPARE(record.raw, QString::fromUtf8(line));
    QCOMPARE(record.message, QStringLiteral("spaced out"));

    // The record's message key groups it with similar messages (computed by LogStore).
    const Record timeout = parseLine("ERROR Payment provider timeout after 3000 ms");
    QCOMPARE(messageKey(timeout.message), QStringLiteral("Payment provider timeout after # ms"));
}

void TestLineParser::invalidUtf8NeverFails() {
    bool ok = true;
    // Bytes that are not UTF-8, a broken two-byte sequence and a NUL.
    constexpr char kGarbage[] = "\xFF\xFE garbage \xC3\x28 \x00 end";
    const QByteArray bytes(kGarbage, sizeof(kGarbage) - 1);
    const Record record = parseLine(bytes, &ok);
    QVERIFY(!ok);
    QVERIFY(record.raw.contains(QChar::ReplacementCharacter));
    QVERIFY(record.raw.endsWith(QStringLiteral("end")));

    // Invalid UTF-8 inside a JSON string makes the line unparsed, never lost.
    const Record json = parseLine("{\"msg\":\"bad \xC3\x28 byte\"}", &ok);
    QVERIFY(!ok);
    QVERIFY(!json.message.isEmpty());
}

void TestLineParser::unicodeText() {
    // Cyrillic and an emoji, written with escapes to keep the source ASCII.
    const QString message = QStringLiteral(u"\u041F\u043B\u0430\u0442\u0451\u0436 \U0001F525");
    const QString service = QStringLiteral(u"\u043E\u043F\u043B\u0430\u0442\u0430");
    const QByteArray json =
        R"({"level":"error","msg":")" + message.toUtf8() + R"(","service":")" + service.toUtf8() + R"("})";
    const Record fromJson = parseLine(json);
    QCOMPARE(fromJson.message, message);
    QCOMPARE(fromJson.source, service);

    const Record fromText = parseLine("2026-11-19 14:05:01 ERROR " + message.toUtf8());
    QCOMPARE(fromText.level, Level::Error);
    QCOMPARE(fromText.message, message);
}
