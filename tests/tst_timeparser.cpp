#include "tst_timeparser.h"

#include "TimeParser.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTest>
#include <QTimeZone>

#include <limits>

namespace {

qint64 utc(int year, int month, int day, int hour, int minute, int second, int msec = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, msec), QTimeZone::UTC)
        .toMSecsSinceEpoch();
}

qint64 local(int year, int month, int day, int hour, int minute, int second, int msec = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, msec), QTimeZone::LocalTime)
        .toMSecsSinceEpoch();
}

} // namespace

void TestTimeParser::parsesTimestamps_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("expected");

    // ISO 8601 with a zone.
    QTest::newRow("Z") << "2026-11-19T14:05:01Z" << utc(2026, 11, 19, 14, 5, 1);
    QTest::newRow("ms and +03:00") << "2026-11-19T14:05:01.123+03:00" << utc(2026, 11, 19, 11, 5, 1, 123);
    QTest::newRow("+0300") << "2026-11-19T14:05:01+0300" << utc(2026, 11, 19, 11, 5, 1);
    QTest::newRow("+03") << "2026-11-19T14:05:01+03" << utc(2026, 11, 19, 11, 5, 1);
    QTest::newRow("-05:30") << "2026-11-19T14:05:01-05:30" << utc(2026, 11, 19, 19, 35, 1);
    QTest::newRow("offset crosses midnight") << "2026-01-01T01:00:00+03:00" << utc(2025, 12, 31, 22, 0, 0);
    QTest::newRow("lower-case t and z") << "2026-11-19t14:05:01z" << utc(2026, 11, 19, 14, 5, 1);
    QTest::newRow("space and Z") << "2026-11-19 14:05:01Z" << utc(2026, 11, 19, 14, 5, 1);
    QTest::newRow("epoch") << "1970-01-01T00:00:00Z" << qint64(0);
    QTest::newRow("before epoch") << "1969-12-31T23:59:59Z" << qint64(-1000);
    QTest::newRow("leap day") << "2028-02-29T12:00:00Z" << utc(2028, 2, 29, 12, 0, 0);

    // Fractions: milliseconds are the first three digits.
    QTest::newRow("one digit") << "2026-11-19T14:05:01.1Z" << utc(2026, 11, 19, 14, 5, 1, 100);
    QTest::newRow("two digits") << "2026-11-19T14:05:01.12Z" << utc(2026, 11, 19, 14, 5, 1, 120);
    QTest::newRow("microseconds") << "2026-11-19T14:05:01.123456Z" << utc(2026, 11, 19, 14, 5, 1, 123);
    QTest::newRow("nanoseconds") << "2026-11-19T14:05:01.999999999Z" << utc(2026, 11, 19, 14, 5, 1, 999);
    QTest::newRow("comma") << "2026-11-19T14:05:01,5Z" << utc(2026, 11, 19, 14, 5, 1, 500);

    // Without a zone the time is local.
    QTest::newRow("local") << "2026-11-19 14:05:01" << local(2026, 11, 19, 14, 5, 1);
    QTest::newRow("local ,zzz") << "2026-11-19 14:05:01,123" << local(2026, 11, 19, 14, 5, 1, 123);
    QTest::newRow("local .zzz") << "2026-11-19 14:05:01.123" << local(2026, 11, 19, 14, 5, 1, 123);
    QTest::newRow("local with T") << "2026-11-19T14:05:01" << local(2026, 11, 19, 14, 5, 1);
    QTest::newRow("summer local") << "2026-07-01 00:00:00" << local(2026, 7, 1, 0, 0, 0);

    // Unix time.
    QTest::newRow("seconds") << "1763560000" << qint64(1'763'560'000'000);
    QTest::newRow("seconds with fraction") << "1763560000.25" << qint64(1'763'560'000'250);
    QTest::newRow("milliseconds") << "1763560000123" << qint64(1'763'560'000'123);

    QTest::newRow("surrounding spaces") << "  2026-11-19T14:05:01Z\t" << utc(2026, 11, 19, 14, 5, 1);
}

void TestTimeParser::parsesTimestamps() {
    QFETCH(QString, text);
    QFETCH(qint64, expected);
    const std::optional<qint64> parsed = TimeParser::parse(text);
    QVERIFY2(parsed, qPrintable(text));
    QCOMPARE(*parsed, expected);
}

void TestTimeParser::rejectsGarbage_data() {
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << "";
    QTest::newRow("spaces") << "   ";
    QTest::newRow("word") << "yesterday";
    QTest::newRow("date only") << "2026-11-19";
    QTest::newRow("no seconds") << "2026-11-19 14:05";
    QTest::newRow("slashes") << "2026/11/19 14:05:01";
    QTest::newRow("month 13") << "2026-13-01 10:00:00";
    QTest::newRow("February 30") << "2026-02-30 10:00:00";
    QTest::newRow("not a leap year") << "2026-02-29 10:00:00";
    QTest::newRow("year zero") << "0000-01-01T00:00:00Z";
    QTest::newRow("hour 24") << "2026-11-19 24:00:00";
    QTest::newRow("minute 60") << "2026-11-19 14:60:00";
    QTest::newRow("second 60") << "2026-11-19 14:05:60";
    QTest::newRow("one-digit hour") << "2026-11-19 4:05:01";
    QTest::newRow("dot without digits") << "2026-11-19T14:05:01.Z";
    QTest::newRow("one-digit offset") << "2026-11-19T14:05:01+3:00";
    QTest::newRow("colon without minutes") << "2026-11-19T14:05:01+03:";
    QTest::newRow("offset hour 24") << "2026-11-19T14:05:01+24:00";
    QTest::newRow("offset minute 60") << "2026-11-19T14:05:01+03:60";
    QTest::newRow("text after timestamp") << "2026-11-19T14:05:01Z done";
    QTest::newRow("9 digits") << "176356000";
    QTest::newRow("11 digits") << "17635600001";
    QTest::newRow("16 digits") << "1763560000123456";
    QTest::newRow("milliseconds with fraction") << "1763560000123.5";
    QTest::newRow("seconds with bare dot") << "1763560000.";
    QTest::newRow("negative seconds") << "-1763560000";
    QTest::newRow("letters around digits") << "x1763560000";
    // "2026" in Arabic-Indic digits: only ASCII digits count.
    QTest::newRow("non-ASCII digits") << QStringLiteral(u"\u0662\u0660\u0662\u0666-11-19T14:05:01Z");
}

void TestTimeParser::rejectsGarbage() {
    QFETCH(QString, text);
    QCOMPARE(TimeParser::parse(text), std::nullopt);
}

void TestTimeParser::findsTimestampAtStartOfText() {
    const QString line = QStringLiteral("2026-11-19 14:05:01,123 ERROR Payment failed");
    const std::optional<TimeParser::Match> match = TimeParser::parsePrefix(line);
    QVERIFY(match);
    QCOMPARE(match->msecs, local(2026, 11, 19, 14, 5, 1, 123));
    QCOMPARE(match->length, 23);

    const std::optional<TimeParser::Match> zoned =
        TimeParser::parsePrefix(u"2026-11-19T14:05:01.123+03:00] x");
    QVERIFY(zoned);
    QCOMPARE(zoned->msecs, utc(2026, 11, 19, 11, 5, 1, 123));
    QCOMPARE(zoned->length, 29);

    QCOMPARE(TimeParser::parsePrefix(u"[2026-11-19 14:05:01]"), std::nullopt);
    QCOMPARE(TimeParser::parsePrefix(u"ERROR 2026-11-19 14:05:01"), std::nullopt);
    QCOMPARE(TimeParser::parsePrefix(u"2026-11-19"), std::nullopt);
}

void TestTimeParser::readsJsonNumbers() {
    QCOMPARE(TimeParser::fromNumber(1'763'560'000), 1'763'560'000'000);
    QCOMPARE(TimeParser::fromNumber(1'763'560'000.25), 1'763'560'000'250);
    QCOMPARE(TimeParser::fromNumber(1'763'560'000'123.0), 1'763'560'000'123);
    QCOMPARE(TimeParser::fromNumber(1'000'000'000), 1'000'000'000'000);
    QCOMPARE(TimeParser::fromNumber(9'999'999'999), 9'999'999'999'000);

    QCOMPARE(TimeParser::fromNumber(0), std::nullopt);
    QCOMPARE(TimeParser::fromNumber(999'999'999), std::nullopt);           // 9 digits
    QCOMPARE(TimeParser::fromNumber(17'635'600'001), std::nullopt);        // 11 digits
    QCOMPARE(TimeParser::fromNumber(1'763'560'000'123'456), std::nullopt); // microseconds
    QCOMPARE(TimeParser::fromNumber(-1'763'560'000), std::nullopt);
    QCOMPARE(TimeParser::fromNumber(std::numeric_limits<double>::quiet_NaN()), std::nullopt);
    QCOMPARE(TimeParser::fromNumber(std::numeric_limits<double>::infinity()), std::nullopt);
}

void TestTimeParser::zonedTimesMatchQDateTime() {
    // The arithmetic for fixed offsets is checked against QDateTime on many instants and offsets.
    QRandomGenerator random(20261119);
    const qint64 first = utc(1971, 1, 1, 0, 0, 0);
    const qint64 last = utc(2199, 12, 31, 23, 59, 59);
    for (int i = 0; i < 2000; ++i) {
        const qint64 msecs = first + qint64(random.bounded(double(last - first)));
        // Whole quarter hours from -12:00 to +14:00, as real zones use.
        const int offsetSeconds = (random.bounded(105) - 48) * 15 * 60;
        const QDateTime moment =
            QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::fromSecondsAheadOfUtc(offsetSeconds));
        const QString text = moment.toString(Qt::ISODateWithMs);
        const std::optional<qint64> parsed = TimeParser::parse(text);
        QVERIFY2(parsed, qPrintable(text));
        QCOMPARE(*parsed, msecs);
    }
}
