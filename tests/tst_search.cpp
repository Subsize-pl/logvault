#include "tst_search.h"

#include "Database.h"
#include "LogStore.h"
#include "Record.h"
#include "SearchQuery.h"
#include "SearchService.h"

#include <QElapsedTimer>
#include <QList>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace {

constexpr qint64 kSecond = 1000;
constexpr qint64 kMinute = 60 * kSecond;
constexpr qint64 kHour = 60 * kMinute;
constexpr qint64 kDay = 24 * kHour;

// 2026-11-20 10:00:00 UTC; the times of the fixture are around it.
constexpr qint64 kT = 1'795'168'800'000;
constexpr int kFixtureSize = 17;

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

// A small shop: written in this order into a new file, the record in the comment gets that id.
QVector<Record> fixtureRecords() {
    const QString gateway = QStringLiteral("gateway");
    const QString payment = QStringLiteral("payment");
    const QString auth = QStringLiteral("auth");
    const QString import = QStringLiteral("import");
    const QString cart = QStringLiteral("cart");
    return {
        /* 1 */ makeRecord(kT, Level::Info, gateway, QStringLiteral("GET /api/cart 200 in 15 ms"),
                           QStringLiteral(R"({"http.status":200,"latency_ms":15,"env":"prod"})")),
        /* 2 */
        makeRecord(kT + kSecond, Level::Info, gateway, QStringLiteral("GET /api/cart 200 in 18 ms"),
                   QStringLiteral(R"({"http.status":200,"latency_ms":18,"env":"prod"})")),
        /* 3 */
        makeRecord(kT + 2 * kSecond, Level::Warn, gateway, QStringLiteral("GET /api/item 404 in 3 ms"),
                   QStringLiteral(R"({"http.status":404,"latency_ms":3,"env":"test"})")),
        /* 4 */
        makeRecord(kT + 3 * kSecond, Level::Error, payment,
                   QStringLiteral("Payment provider timeout after 3000 ms"),
                   QStringLiteral(R"({"provider":"bepaid","order_id":8812,"env":"prod"})")),
        /* 5: the same time as 4 */
        makeRecord(kT + 3 * kSecond, Level::Error, payment,
                   QStringLiteral("Payment provider timeout after 4500 ms"),
                   QStringLiteral(R"({"provider":"BePaid","order_id":8813,"env":"prod"})")),
        /* 6 */
        makeRecord(kT + 4 * kSecond, Level::Fatal, payment,
                   QStringLiteral("Circuit breaker opened for provider bepaid"),
                   QStringLiteral(R"({"provider":"bepaid"})")),
        /* 7 */
        makeRecord(kT + 5 * kSecond, Level::Info, payment,
                   QStringLiteral("Payment for order 8814 authorized by stripe in 210 ms"),
                   QStringLiteral(R"({"provider":"stripe","order_id":"8814","amount":12.5})")),
        /* 8 */
        makeRecord(kT + kMinute, Level::Debug, auth, QStringLiteral("User 42 logged in"),
                   QStringLiteral(R"({"user":"admin","cached":true})")),
        /* 9 */
        makeRecord(kT + kMinute + kSecond, Level::Debug, auth, QStringLiteral("User 43 logged in"),
                   QStringLiteral(R"({"user":"administrator","cached":false})")),
        /* 10 */
        makeRecord(kT + kMinute + 2 * kSecond, Level::Trace, auth,
                   QStringLiteral("Token check: 100% valid_token"), QStringLiteral(R"({"user":"Admin"})")),
        /* 11 */
        makeRecord(kT + 2 * kMinute, Level::Unknown, import, QStringLiteral("unparsed line"),
                   QStringLiteral(R"({"_parse_error":true})")),
        /* 12 */
        makeRecord(kT + 2 * kMinute + kSecond, Level::Warn, import, QStringLiteral("Disk usage at 91%")),
        /* 13 */
        makeRecord(kT + kHour, Level::Info, gateway, QStringLiteral("GET /api/cart 200 in 20 ms"),
                   QStringLiteral(R"({"http.status":"200","env":"prod"})")),
        /* 14 */
        makeRecord(kT + kHour + kSecond, Level::Error, cart, QStringLiteral("Cart 7 not found"),
                   QStringLiteral(R"({"cart_id":7,"env":null})")),
        /* 15 */
        makeRecord(kT + 2 * kHour, Level::Info, cart, QStringLiteral("Connection refused by db")),
        /* 16 */ makeRecord(kT - kDay, Level::Info, gateway, QStringLiteral("Health check ok")),
        /* 17 */
        makeRecord(
            kT + 6 * kSecond, Level::Error, payment,
            QStringLiteral(u"\u041E\u0448\u0438\u0431\u043A\u0430 \u043E\u043F\u043B\u0430\u0442\u044B")),
    };
}

// A database open in the test thread. The store is declared after the database, so it goes first.
class TestDatabase {
public:
    bool open(const QString& path, const QVector<Record>& records) {
        if (!m_database.open(path)) {
            m_error = m_database.errorString();
            return false;
        }
        m_store.emplace(m_database);
        if (!m_store->insertBatch(records)) {
            m_error = m_store->errorString();
            return false;
        }
        return true;
    }

    LogStore& store() { return *m_store; }
    QString error() const { return m_error; }

private:
    Database m_database;
    std::optional<LogStore> m_store;
    QString m_error;
};

SqlFragment conditionFor(const QString& text) {
    SearchRequest request;
    request.text = text;
    return SearchQuery::build(request);
}

// Counts the matches the way a search does: the bars of a histogram over the span of the matches.
std::optional<qint64> matchCount(LogStore& store, const SqlFragment& condition) {
    const std::optional<MatchSpan> span = store.matchSpan(condition);
    if (!span) {
        return std::nullopt;
    }
    if (!span->found) {
        return 0;
    }
    const std::optional<Histogram> histogram = store.histogram(condition, span->firstTs, span->lastTs);
    if (!histogram) {
        return std::nullopt;
    }
    return histogram->total();
}

QList<qint64> idsOf(const QVector<Record>& records) {
    QList<qint64> ids;
    for (const Record& record : records) {
        ids.append(record.id);
    }
    return ids;
}

QList<qint64> sortedIdsOf(const QVector<Record>& records) {
    QList<qint64> ids = idsOf(records);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// The ids of the fixture without the given ones.
QList<qint64> allExcept(const QList<qint64>& excluded) {
    QList<qint64> ids;
    for (qint64 id = 1; id <= kFixtureSize; ++id) {
        if (!excluded.contains(id)) {
            ids.append(id);
        }
    }
    return ids;
}

QList<qint64> asList(const LevelCounts& counts) { return QList<qint64>(counts.begin(), counts.end()); }

LevelCounts levelCounts(std::initializer_list<std::pair<Level, qint64>> counts) {
    LevelCounts result{};
    for (const auto& [level, count] : counts) {
        result[static_cast<int>(level)] = count;
    }
    return result;
}

QStringList valuesOf(const QVector<StatsRow>& rows) {
    QStringList values;
    for (const StatsRow& row : rows) {
        values.append(row.value);
    }
    return values;
}

QList<qint64> countsOf(const QVector<StatsRow>& rows) {
    QList<qint64> counts;
    for (const StatsRow& row : rows) {
        counts.append(row.count);
    }
    return counts;
}

qint64 sourceId(const QVector<SourceInfo>& sources, const QString& name) {
    for (const SourceInfo& source : sources) {
        if (source.name == name) {
            return source.id;
        }
    }
    return 0;
}

// The bar widths of DESIGN.md section 6.4.
constexpr std::array<qint64, 12> kSteps = {kSecond,     5 * kSecond,  15 * kSecond, kMinute,
                                           5 * kMinute, 15 * kMinute, kHour,        3 * kHour,
                                           12 * kHour,  kDay,         7 * kDay,     30 * kDay};

qint64 floorToStep(qint64 value, qint64 step) {
    const qint64 remainder = value % step;
    return remainder < 0 ? value - remainder - step : value - remainder;
}

qint64 barsFor(qint64 first, qint64 last, qint64 step) {
    return (last - floorToStep(first, step)) / step + 1;
}

// Two letters per number below 676, so generated texts differ without digits (msg_key replaces those).
QString letters(int number) { return QString(QChar(u'a' + number / 26)) + QChar(u'a' + number % 26); }

// One record of the generated shop of the performance test; the same index always gives the same record.
Record generatedRecord(int index, QRandomGenerator& random) {
    static const QStringList services = {QStringLiteral("gateway"), QStringLiteral("auth"),
                                         QStringLiteral("cart"), QStringLiteral("payment")};
    static const QStringList providers = {QStringLiteral("bepaid"), QStringLiteral("stripe"),
                                          QStringLiteral("paypal")};
    static const QStringList paths = {QStringLiteral("cart"), QStringLiteral("order"),
                                      QStringLiteral("item")};

    const int roll = random.bounded(1000);
    const Level level = roll < 20    ? Level::Trace
                        : roll < 170 ? Level::Debug
                        : roll < 820 ? Level::Info
                        : roll < 920 ? Level::Warn
                        : roll < 990 ? Level::Error
                        : roll < 995 ? Level::Fatal
                                     : Level::Unknown;
    const QString service = services[random.bounded(4)];
    const int user = random.bounded(5000);
    QString message;
    QString fields = QStringLiteral(R"("user_id":%1,"env":"%2")")
                         .arg(user)
                         .arg(random.bounded(10) == 0 ? QStringLiteral("test") : QStringLiteral("prod"));
    if (service == QLatin1String("payment")) {
        const int order = 10000 + index;
        const QString provider = level == Level::Error && random.bounded(5) != 0
                                     ? QStringLiteral("bepaid")
                                     : providers[random.bounded(providers.size())];
        const int latency = level == Level::Error ? 3000 + random.bounded(2000) : 150 + random.bounded(750);
        message = level == Level::Error ? QStringLiteral("Payment provider timeout after %1 ms").arg(latency)
                                        : QStringLiteral("Payment for order %1 authorized by %2 in %3 ms")
                                              .arg(order)
                                              .arg(provider)
                                              .arg(latency);
        fields += QStringLiteral(R"(,"provider":"%1","order_id":%2,"latency_ms":%3)")
                      .arg(provider)
                      .arg(order)
                      .arg(latency);
    } else if (service == QLatin1String("gateway")) {
        const int status = level == Level::Error ? 500 : level == Level::Warn ? 404 : 200;
        const int latency = 5 + random.bounded(175);
        message = QStringLiteral("GET /api/%1 %2 in %3 ms")
                      .arg(paths[random.bounded(paths.size())])
                      .arg(status)
                      .arg(latency);
        fields += QStringLiteral(R"(,"http.status":%1,"latency_ms":%2)").arg(status).arg(latency);
    } else if (service == QLatin1String("auth")) {
        message = level >= Level::Warn ? QStringLiteral("Invalid password for user %1").arg(user)
                                       : QStringLiteral("User %1 logged in").arg(user);
    } else {
        const int cartId = random.bounded(100000);
        message = level >= Level::Warn
                      ? QStringLiteral("Cart %1 not found").arg(cartId)
                      : QStringLiteral("Item %1 added to cart %2").arg(random.bounded(900)).arg(cartId);
        fields += QStringLiteral(R"(,"cart_id":%1)").arg(cartId);
    }

    Record record = makeRecord(kT - 7 * kDay + qint64(index) * 600, level, service, message,
                               QLatin1Char('{') + fields + QLatin1Char('}'));
    record.raw = QStringLiteral(R"({"time":%1,"level":"%2","service":"%3","msg":"%4",%5})")
                     .arg(record.ts)
                     .arg(levelName(level).toLower(), service, message, fields);
    return record;
}

} // namespace

void TestSearch::init() {
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestSearch::cleanup() { m_dir.reset(); }

QString TestSearch::databasePath() const { return m_dir->filePath(QStringLiteral("search.lvdb")); }

void TestSearch::ordersNewestFirst() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));

    const std::optional<QVector<Record>> rows = db.store().findRecords({}, 100);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    // By time, newest first; records 4 and 5 share a time, so the later insert (5) comes first.
    QCOMPARE(idsOf(*rows), (QList<qint64>{15, 14, 13, 12, 11, 10, 9, 8, 17, 7, 6, 5, 4, 3, 2, 1, 16}));

    const Record& first = rows->first();
    QCOMPARE(first.ts, kT + 2 * kHour);
    QCOMPARE(first.level, Level::Info);
    QCOMPARE(first.source, QStringLiteral("cart"));
    QCOMPARE(first.message, QStringLiteral("Connection refused by db"));
    QVERIFY(first.fields.isEmpty());
    QCOMPARE(rows->at(13).fields, QStringLiteral(R"({"http.status":404,"latency_ms":3,"env":"test"})"));
}

void TestSearch::limitsRows() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    LogStore& store = db.store();

    QCOMPARE(idsOf(store.findRecords({}, 3).value()), (QList<qint64>{15, 14, 13}));
    QCOMPARE(idsOf(store.findRecords(conditionFor(QStringLiteral("provider:*")), 2).value()),
             (QList<qint64>{7, 6}));
    QVERIFY(store.findRecords({}, 0).value().isEmpty());
    // SQLite reads a negative LIMIT as "no limit"; the store must not.
    QVERIFY(store.findRecords({}, -1).value().isEmpty());
}

void TestSearch::findsTheSpanOfMatches() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    LogStore& store = db.store();

    std::optional<MatchSpan> span = store.matchSpan({});
    QVERIFY2(span, qPrintable(store.errorString()));
    QVERIFY(span->found);
    QCOMPARE(span->firstTs, kT - kDay);
    QCOMPARE(span->lastTs, kT + 2 * kHour);

    span = store.matchSpan(conditionFor(QStringLiteral("logged")));
    QVERIFY2(span, qPrintable(store.errorString()));
    QVERIFY(span->found);
    QCOMPARE(span->firstTs, kT + kMinute);
    QCOMPARE(span->lastTs, kT + kMinute + kSecond);

    span = store.matchSpan(conditionFor(QStringLiteral("no-such-text")));
    QVERIFY2(span, qPrintable(store.errorString()));
    QVERIFY(!span->found);
}

void TestSearch::searchLineMatchesRecords_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QList<qint64>>("ids");

    QTest::newRow("everything") << QString() << allExcept({});
    QTest::newRow("word") << QStringLiteral("timeout") << QList<qint64>{4, 5};
    QTest::newRow("Latin letters ignore case") << QStringLiteral("TIMEOUT") << QList<qint64>{4, 5};
    QTest::newRow("phrase") << QStringLiteral(R"("provider timeout")") << QList<qint64>{4, 5};
    QTest::newRow("phrase keeps the word order")
        << QStringLiteral(R"("timeout provider")") << QList<qint64>{};
    QTest::newRow("phrase ignores case") << QStringLiteral(R"("connection refused")") << QList<qint64>{15};
    QTest::newRow("every word must match") << QStringLiteral("logged in") << QList<qint64>{8, 9};
    QTest::newRow("excluded word") << QStringLiteral("logged -43") << QList<qint64>{8};
    QTest::newRow("only an excluded word") << QStringLiteral("-timeout") << allExcept({4, 5});
    QTest::newRow("the source is not text") << QStringLiteral("gateway") << QList<qint64>{};
    QTest::newRow("field value ignores case") << QStringLiteral("provider:BEPAID") << QList<qint64>{4, 5, 6};
    QTest::newRow("field pattern") << QStringLiteral("provider:bep*") << QList<qint64>{4, 5, 6};
    QTest::newRow("field exists") << QStringLiteral("provider:*") << QList<qint64>{4, 5, 6, 7};
    QTest::newRow("field is missing") << QStringLiteral("-provider:*") << allExcept({4, 5, 6, 7});
    QTest::newRow("excluded value keeps records without the field")
        << QStringLiteral("-provider:bepaid") << allExcept({4, 5, 6});
    QTest::newRow("number field") << QStringLiteral("order_id:8812") << QList<qint64>{4};
    QTest::newRow("string field with digits") << QStringLiteral("order_id:8814") << QList<qint64>{7};
    QTest::newRow("number and string are equal")
        << QStringLiteral("http.status:200") << QList<qint64>{1, 2, 13};
    QTest::newRow("pattern on numbers") << QStringLiteral("http.status:2*") << QList<qint64>{1, 2, 13};
    QTest::newRow("nested field exists") << QStringLiteral("http.status:*") << QList<qint64>{1, 2, 3, 13};
    QTest::newRow("excluded field value") << QStringLiteral("-env:prod") << allExcept({1, 2, 4, 5, 13});
    QTest::newRow("null is no field") << QStringLiteral("env:*") << QList<qint64>{1, 2, 3, 4, 5, 13};
    QTest::newRow("true") << QStringLiteral("cached:true") << QList<qint64>{8};
    QTest::newRow("false ignores case") << QStringLiteral("cached:FALSE") << QList<qint64>{9};
    QTest::newRow("booleans are words, not numbers") << QStringLiteral("cached:1") << QList<qint64>{};
    QTest::newRow("unparsed lines") << QStringLiteral("_parse_error:true") << QList<qint64>{11};
    QTest::newRow("decimal number") << QStringLiteral("amount:12.5") << QList<qint64>{7};
    QTest::newRow("pattern ignores case") << QStringLiteral("user:adm*") << QList<qint64>{8, 9, 10};
    QTest::newRow("equality ignores case") << QStringLiteral("user:admin") << QList<qint64>{8, 10};
    QTest::newRow("percent sign") << QStringLiteral("100%") << QList<qint64>{10};
    QTest::newRow("percent sign is no wildcard") << QStringLiteral("1%") << QList<qint64>{12};
    QTest::newRow("underscore") << QStringLiteral("valid_token") << QList<qint64>{10};
    QTest::newRow("underscore is no wildcard") << QStringLiteral("Token_check") << QList<qint64>{};
    QTest::newRow("name without value is text") << QStringLiteral("check:") << QList<qint64>{10};
    QTest::newRow("cyrillic") << QStringLiteral(u"\u041E\u0448\u0438\u0431\u043A\u0430") << QList<qint64>{17};
    // SQLite folds the case of Latin letters only.
    QTest::newRow("cyrillic keeps its case")
        << QStringLiteral(u"\u043E\u0448\u0438\u0431\u043A\u0430") << QList<qint64>{};
}

void TestSearch::searchLineMatchesRecords() {
    QFETCH(QString, text);
    QFETCH(QList<qint64>, ids);

    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    const SqlFragment condition = conditionFor(text);
    const std::optional<QVector<Record>> rows = db.store().findRecords(condition, 100);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    QCOMPARE(sortedIdsOf(*rows), ids);
    QCOMPARE(matchCount(db.store(), condition).value_or(-1), ids.size());
}

void TestSearch::injectionAttemptsChangeNothing() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    LogStore& store = db.store();

    const QStringList attempts = {
        QStringLiteral("'; DROP TABLE records; --"),
        QStringLiteral(R"(provider:"x' OR '1'='1")"),
        QStringLiteral(R"(provider:bepaid' OR 1=1 --)"),
        QStringLiteral("x');DROP/**/TABLE/**/records;--:1"),
        QStringLiteral(R"(") OR 1=1 --)"),
    };
    for (const QString& text : attempts) {
        const std::optional<QVector<Record>> rows = store.findRecords(conditionFor(text), 100);
        QVERIFY2(rows, qPrintable(text + QStringLiteral(": ") + store.errorString()));
        QVERIFY2(rows->isEmpty(), qPrintable(text));
    }
    QCOMPARE(store.recordCount().value_or(-1), kFixtureSize);
    QCOMPARE(store.sources().value().size(), 5);
}

void TestSearch::filtersMatchRecords_data() {
    QTest::addColumn<QList<int>>("levels");
    QTest::addColumn<QStringList>("sources");
    QTest::addColumn<qint64>("from");
    QTest::addColumn<qint64>("to");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QList<qint64>>("ids");

    const QList<int> all = {0, 1, 2, 3, 4, 5, 6};
    QTest::newRow("all levels") << all << QStringList() << qint64(0) << qint64(0) << QString()
                                << allExcept({});
    QTest::newRow("one level") << QList<int>{static_cast<int>(Level::Error)} << QStringList() << qint64(0)
                               << qint64(0) << QString() << QList<qint64>{4, 5, 14, 17};
    QTest::newRow("unknown and trace")
        << QList<int>{static_cast<int>(Level::Unknown), static_cast<int>(Level::Trace)} << QStringList()
        << qint64(0) << qint64(0) << QString() << QList<qint64>{10, 11};
    QTest::newRow("no level") << QList<int>() << QStringList() << qint64(0) << qint64(0) << QString()
                              << QList<qint64>{};
    QTest::newRow("one source") << all << QStringList{QStringLiteral("payment")} << qint64(0) << qint64(0)
                                << QString() << QList<qint64>{4, 5, 6, 7, 17};
    QTest::newRow("two sources") << all << QStringList{QStringLiteral("payment"), QStringLiteral("cart")}
                                 << qint64(0) << qint64(0) << QString()
                                 << QList<qint64>{4, 5, 6, 7, 14, 15, 17};
    // From is inclusive (4 and 5 are at exactly from), to is exclusive (7 is at exactly to).
    QTest::newRow("period bounds") << all << QStringList() << kT + 3 * kSecond << kT + 5 * kSecond
                                   << QString() << QList<qint64>{4, 5, 6};
    QTest::newRow("from only") << all << QStringList() << kT + kHour << qint64(0) << QString()
                               << QList<qint64>{13, 14, 15};
    QTest::newRow("to only") << all << QStringList() << qint64(0) << kT << QString() << QList<qint64>{16};
    QTest::newRow("everything at once")
        << QList<int>{static_cast<int>(Level::Error), static_cast<int>(Level::Fatal)}
        << QStringList{QStringLiteral("payment")} << kT << kT + kHour << QStringLiteral("provider:bepaid")
        << QList<qint64>{4, 5, 6};
    QTest::newRow("filters and an exclusion")
        << QList<int>{static_cast<int>(Level::Info)} << QStringList{QStringLiteral("gateway")} << qint64(0)
        << qint64(0) << QStringLiteral("-env:test") << QList<qint64>{1, 2, 13, 16};
}

void TestSearch::filtersMatchRecords() {
    QFETCH(QList<int>, levels);
    QFETCH(QStringList, sources);
    QFETCH(qint64, from);
    QFETCH(qint64, to);
    QFETCH(QString, text);
    QFETCH(QList<qint64>, ids);

    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    const QVector<SourceInfo> sourceList = db.store().sources().value();

    SearchRequest request;
    request.levels.clear();
    for (const int level : levels) {
        request.levels.append(static_cast<Level>(level));
    }
    for (const QString& name : sources) {
        request.sourceIds.append(sourceId(sourceList, name));
    }
    request.from = from;
    request.to = to;
    request.text = text;
    const SqlFragment condition = SearchQuery::build(request);
    const std::optional<QVector<Record>> rows = db.store().findRecords(condition, 100);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    QCOMPARE(sortedIdsOf(*rows), ids);
    QCOMPARE(matchCount(db.store(), condition).value_or(-1), ids.size());
}

void TestSearch::hugeSearchLineDoesNotCrash() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    QStringList words;
    for (int i = 0; i < 3000; ++i) {
        words.append(QStringLiteral("w%1").arg(i));
    }
    const std::optional<MatchSpan> span = db.store().matchSpan(conditionFor(words.join(u' ')));
    // SQLite may refuse a condition this deep. Either way the answer is clean: no matches or an error.
    if (span) {
        QVERIFY(!span->found);
    } else {
        QVERIFY(!db.store().errorString().isEmpty());
    }
}

void TestSearch::histogramCountsEveryMatch() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    LogStore& store = db.store();

    // Two minutes from kT: 120 bars of a second would be too many, so the bars are 5 seconds wide.
    SearchRequest request;
    request.from = kT;
    request.to = kT + 2 * kMinute;
    const SqlFragment condition = SearchQuery::build(request);
    QCOMPARE(matchCount(store, condition).value_or(-1), 11);
    std::optional<Histogram> histogram = store.histogram(condition, request.from, request.to - 1);
    QVERIFY2(histogram, qPrintable(store.errorString()));
    QCOMPARE(histogram->start, kT);
    QCOMPARE(histogram->step, 5 * kSecond);
    QCOMPARE(histogram->bars.size(), 24);
    for (qsizetype bar = 0; bar < histogram->bars.size(); ++bar) {
        LevelCounts expected{};
        if (bar == 0) {
            expected =
                levelCounts({{Level::Info, 2}, {Level::Warn, 1}, {Level::Error, 2}, {Level::Fatal, 1}});
        } else if (bar == 1) {
            expected = levelCounts({{Level::Info, 1}, {Level::Error, 1}});
        } else if (bar == 12) {
            expected = levelCounts({{Level::Trace, 1}, {Level::Debug, 2}});
        }
        QCOMPARE(asList(histogram->bars[bar]), asList(expected));
    }

    // All time: the bars span the matches, and every match is in exactly one bar.
    const MatchSpan all = store.matchSpan({}).value();
    histogram = store.histogram({}, all.firstTs, all.lastTs);
    QVERIFY2(histogram, qPrintable(store.errorString()));
    QCOMPARE(histogram->start, kT - kDay);
    QCOMPARE(histogram->step, kHour);
    QCOMPARE(histogram->bars.size(), 27);
    LevelCounts perLevel{};
    qint64 total = 0;
    for (const LevelCounts& bar : histogram->bars) {
        for (int level = 0; level < kLevelCount; ++level) {
            perLevel[level] += bar[level];
            total += bar[level];
        }
    }
    QCOMPARE(total, kFixtureSize);
    QCOMPARE(asList(perLevel), asList(levelCounts({{Level::Unknown, 1},
                                                   {Level::Trace, 1},
                                                   {Level::Debug, 2},
                                                   {Level::Info, 6},
                                                   {Level::Warn, 2},
                                                   {Level::Error, 4},
                                                   {Level::Fatal, 1}})));
    QCOMPARE(asList(histogram->bars[0]), asList(levelCounts({{Level::Info, 1}})));
}

void TestSearch::histogramStep_data() {
    QTest::addColumn<qint64>("first");
    QTest::addColumn<qint64>("last");
    QTest::addColumn<qint64>("start");
    QTest::addColumn<qint64>("step");
    QTest::addColumn<int>("bars");

    // 2026-11-20 00:00:00 UTC, a multiple of every step up to one day.
    constexpr qint64 t0 = 1'795'132'800'000;
    QTest::newRow("one instant") << t0 << t0 << t0 << kSecond << 1;
    QTest::newRow("last before first") << t0 << t0 - 5 * kSecond << t0 << kSecond << 1;
    QTest::newRow("one minute") << t0 << t0 + kMinute - 1 << t0 << kSecond << 60;
    QTest::newRow("80 seconds") << t0 << t0 + 80 * kSecond - 1 << t0 << kSecond << 80;
    QTest::newRow("81 seconds") << t0 << t0 + 81 * kSecond - 1 << t0 << 5 * kSecond << 17;
    // Aligning the start down adds a bar: 1-second bars would be 81.
    QTest::newRow("unaligned start") << t0 + 500 << t0 + 80 * kSecond + 499 << t0 << 5 * kSecond << 17;
    QTest::newRow("15 minutes") << t0 << t0 + 15 * kMinute - 1 << t0 << 15 * kSecond << 60;
    QTest::newRow("one hour") << t0 << t0 + kHour - 1 << t0 << kMinute << 60;
    QTest::newRow("6 hours") << t0 << t0 + 6 * kHour - 1 << t0 << 5 * kMinute << 72;
    QTest::newRow("one day") << t0 << t0 + kDay - 1 << t0 << kHour << 24;
    QTest::newRow("7 days") << t0 << t0 + 7 * kDay - 1 << t0 << 3 * kHour << 56;
    QTest::newRow("30 days") << t0 << t0 + 30 * kDay - 1 << t0 << 12 * kHour << 60;
    QTest::newRow("one year") << t0 << t0 + 365 * kDay - 1 << t0 - kDay << 7 * kDay << 53;
    QTest::newRow("ten years") << t0 << t0 + 3650 * kDay - 1 << t0 - 17 * kDay << 60 * kDay << 62;
    QTest::newRow("before 1970") << qint64(-12'345) << qint64(-1) << qint64(-13'000) << kSecond << 13;
    QTest::newRow("across 1970") << qint64(-30'000) << qint64(29'999) << qint64(-30'000) << kSecond << 60;
}

void TestSearch::histogramStep() {
    QFETCH(qint64, first);
    QFETCH(qint64, last);
    QFETCH(qint64, start);
    QFETCH(qint64, step);
    QFETCH(int, bars);

    const Histogram histogram = Histogram::forRange(first, last);
    QCOMPARE(histogram.start, start);
    QCOMPARE(histogram.step, step);
    QCOMPARE(histogram.bars.size(), bars);
    for (const LevelCounts& bar : histogram.bars) {
        QCOMPARE(asList(bar), asList(LevelCounts{}));
    }
}

void TestSearch::histogramStepFitsAnyRange() {
    QRandomGenerator random(20261120);
    for (int i = 0; i < 5000; ++i) {
        const qint64 first = random.bounded(-100'000'000'000'000LL, 100'000'000'000'000LL);
        // Spans from a millisecond to about 300 years, spread over every step.
        const qint64 span = qint64(std::pow(10.0, random.bounded(13.0)));
        const qint64 last = first + span;

        const Histogram histogram = Histogram::forRange(first, last);
        const qint64 step = histogram.step;
        const QString range = QStringLiteral("%1..%2").arg(first).arg(last);
        QVERIFY2(step > 0 && histogram.start % step == 0, qPrintable(range));
        QVERIFY2(histogram.start <= first && histogram.start + histogram.bars.size() * step > last,
                 qPrintable(range));
        QVERIFY2(!histogram.bars.isEmpty() && histogram.bars.size() <= Histogram::kMaxBars,
                 qPrintable(range));
        QCOMPARE(histogram.bars.size(), barsFor(first, last, step));
        // The step is the finest one that fits.
        const auto listed = std::find(kSteps.begin(), kSteps.end(), step);
        if (listed != kSteps.end()) {
            QVERIFY2(listed == kSteps.begin() || barsFor(first, last, *(listed - 1)) > Histogram::kMaxBars,
                     qPrintable(range));
        } else {
            QVERIFY2(step % kSteps.back() == 0 &&
                         barsFor(first, last, step - kSteps.back()) > Histogram::kMaxBars,
                     qPrintable(range));
        }
    }

    // Times far outside any log stay within the bar limit and do not overflow.
    const qint64 lowest = std::numeric_limits<qint64>::min();
    const qint64 highest = std::numeric_limits<qint64>::max();
    for (const auto& [first, last] :
         {std::pair{lowest, highest}, std::pair{highest, highest}, std::pair{lowest, lowest}}) {
        const Histogram histogram = Histogram::forRange(first, last);
        QVERIFY(histogram.step > 0);
        QVERIFY(!histogram.bars.isEmpty() && histogram.bars.size() <= Histogram::kMaxBars);
    }
}

void TestSearch::statisticsByLevel() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));

    const std::optional<QVector<StatsRow>> rows = db.store().statistics({}, StatsMode::Level);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    // Largest first; equal counts by level value.
    QCOMPARE(valuesOf(*rows),
             (QStringList{QStringLiteral("INFO"), QStringLiteral("ERROR"), QStringLiteral("DEBUG"),
                          QStringLiteral("WARN"), QStringLiteral("UNKNOWN"), QStringLiteral("TRACE"),
                          QStringLiteral("FATAL")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{6, 4, 2, 2, 1, 1, 1}));
    QList<qint64> levels;
    for (const StatsRow& row : *rows) {
        levels.append(row.id);
        QVERIFY(!row.noField);
    }
    QCOMPARE(levels, (QList<qint64>{3, 5, 2, 4, 0, 1, 6}));
}

void TestSearch::statisticsBySource() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));

    const std::optional<QVector<StatsRow>> rows = db.store().statistics({}, StatsMode::Source);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    QCOMPARE(valuesOf(*rows),
             (QStringList{QStringLiteral("gateway"), QStringLiteral("payment"), QStringLiteral("auth"),
                          QStringLiteral("cart"), QStringLiteral("import")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{5, 5, 3, 2, 2}));
    const QVector<SourceInfo> sources = db.store().sources().value();
    for (const StatsRow& row : *rows) {
        QCOMPARE(row.id, sourceId(sources, row.value));
    }
}

void TestSearch::statisticsByMessage() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));

    const std::optional<QVector<StatsRow>> rows = db.store().statistics({}, StatsMode::Message);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    // Digits are grouped into '#'; equal counts are ordered by the pattern (byte order).
    QCOMPARE(
        valuesOf(*rows),
        (QStringList{
            QStringLiteral("GET /api/cart # in # ms"), QStringLiteral("Payment provider timeout after # ms"),
            QStringLiteral("User # logged in"), QStringLiteral("Cart # not found"),
            QStringLiteral("Circuit breaker opened for provider bepaid"),
            QStringLiteral("Connection refused by db"), QStringLiteral("Disk usage at #%"),
            QStringLiteral("GET /api/item # in # ms"), QStringLiteral("Health check ok"),
            QStringLiteral("Payment for order # authorized by stripe in # ms"),
            QStringLiteral("Token check: #% valid_token"), QStringLiteral("unparsed line"),
            QStringLiteral(u"\u041E\u0448\u0438\u0431\u043A\u0430 \u043E\u043F\u043B\u0430\u0442\u044B")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}));
}

void TestSearch::statisticsByField_data() {
    QTest::addColumn<QString>("field");
    QTest::addColumn<QStringList>("values");
    QTest::addColumn<QList<qint64>>("counts");

    const QString noField = QStringLiteral("(no field)");
    // Groups keep the exact value: "BePaid" is its own group, although the search ignores case.
    QTest::newRow("text") << QStringLiteral("provider")
                          << QStringList{noField, QStringLiteral("bepaid"), QStringLiteral("BePaid"),
                                         QStringLiteral("stripe")}
                          << QList<qint64>{13, 2, 1, 1};
    QTest::newRow("booleans") << QStringLiteral("cached")
                              << QStringList{noField, QStringLiteral("false"), QStringLiteral("true")}
                              << QList<qint64>{15, 1, 1};
    QTest::newRow("numbers and strings")
        << QStringLiteral("http.status") << QStringList{noField, QStringLiteral("200"), QStringLiteral("404")}
        << QList<qint64>{13, 3, 1};
    QTest::newRow("null is no field")
        << QStringLiteral("env") << QStringList{noField, QStringLiteral("prod"), QStringLiteral("test")}
        << QList<qint64>{11, 5, 1};
    QTest::newRow("unknown field") << QStringLiteral("nothing") << QStringList{noField}
                                   << QList<qint64>{kFixtureSize};
}

void TestSearch::statisticsByField() {
    QFETCH(QString, field);
    QFETCH(QStringList, values);
    QFETCH(QList<qint64>, counts);

    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    const std::optional<QVector<StatsRow>> rows = db.store().fieldStatistics({}, field, kFixtureSize);
    QVERIFY2(rows, qPrintable(db.store().errorString()));
    QCOMPARE(valuesOf(*rows), values);
    QCOMPARE(countsOf(*rows), counts);
    for (qsizetype i = 0; i < rows->size(); ++i) {
        QCOMPARE(rows->at(i).noField, i == 0);
        QCOMPARE(rows->at(i).id, 0);
    }
}

void TestSearch::statisticsFollowTheCondition() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    LogStore& store = db.store();

    std::optional<QVector<StatsRow>> rows =
        store.statistics(conditionFor(QStringLiteral("provider:*")), StatsMode::Level);
    QVERIFY2(rows, qPrintable(store.errorString()));
    QCOMPARE(valuesOf(*rows),
             (QStringList{QStringLiteral("ERROR"), QStringLiteral("INFO"), QStringLiteral("FATAL")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{2, 1, 1}));

    SearchRequest request;
    request.levels = {Level::Error};
    rows = store.statistics(SearchQuery::build(request), StatsMode::Source);
    QVERIFY2(rows, qPrintable(store.errorString()));
    QCOMPARE(valuesOf(*rows), (QStringList{QStringLiteral("payment"), QStringLiteral("cart")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{3, 1}));

    rows = store.fieldStatistics(conditionFor(QStringLiteral("provider:*")), QStringLiteral("provider"), 4);
    QVERIFY2(rows, qPrintable(store.errorString()));
    QCOMPARE(valuesOf(*rows),
             (QStringList{QStringLiteral("bepaid"), QStringLiteral("BePaid"), QStringLiteral("stripe")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{2, 1, 1}));

    rows = store.statistics(conditionFor(QStringLiteral("no-such-text")), StatsMode::Message);
    QVERIFY2(rows, qPrintable(store.errorString()));
    QVERIFY(rows->isEmpty());
}

void TestSearch::statisticsAreLimited() {
    // 150 different messages and codes, and 50 records without a code.
    QVector<Record> records;
    for (int i = 0; i < 150; ++i) {
        records.append(makeRecord(kT + i, Level::Info, QStringLiteral("app"),
                                  QStringLiteral("event ") + letters(i),
                                  QStringLiteral(R"({"code":"%1"})").arg(letters(i))));
    }
    for (int i = 0; i < 50; ++i) {
        records.append(
            makeRecord(kT + 150 + i, Level::Info, QStringLiteral("app"), QStringLiteral("plain record")));
    }
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), records), qPrintable(db.error()));
    LogStore& store = db.store();

    const std::optional<QVector<StatsRow>> messages = store.statistics({}, StatsMode::Message);
    QVERIFY2(messages, qPrintable(store.errorString()));
    QCOMPARE(messages->size(), LogStore::kStatsRowLimit);
    QCOMPARE(messages->first().value, QStringLiteral("plain record"));
    QCOMPARE(messages->first().count, 50);
    QCOMPARE(messages->at(1).value, QStringLiteral("event aa"));

    // The records without the field are one group, counted from the total, and the limit includes it.
    const std::optional<QVector<StatsRow>> codes = store.fieldStatistics({}, QStringLiteral("code"), 200);
    QVERIFY2(codes, qPrintable(store.errorString()));
    QCOMPARE(codes->size(), LogStore::kStatsRowLimit);
    QVERIFY(codes->first().noField);
    QCOMPARE(codes->first().count, 50);
    QCOMPARE(codes->at(1).value, QStringLiteral("aa"));
    QCOMPARE(codes->last().value, letters(98));

    // Levels and sources are few, so they are never cut.
    QCOMPARE(store.statistics({}, StatsMode::Level).value().size(), 1);
    QCOMPARE(store.statistics({}, StatsMode::Source).value().size(), 1);
}

void TestSearch::statisticsRejectBadFieldName() {
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), fixtureRecords()), qPrintable(db.error()));
    LogStore& store = db.store();

    for (const QString& field : {QString(), QStringLiteral("a b"), QStringLiteral("x'),'$")}) {
        QVERIFY(!store.fieldStatistics({}, field, kFixtureSize));
        QVERIFY2(store.errorString().contains(QStringLiteral("not a valid field name")),
                 qPrintable(store.errorString()));
    }
    // statistics() groups by level, source and message only.
    for (const StatsMode mode : {StatsMode::None, StatsMode::Field}) {
        const std::optional<QVector<StatsRow>> rows = store.statistics({}, mode);
        QVERIFY(rows && rows->isEmpty());
    }
}

void TestSearch::fieldMentionIsOnlyAHint() {
    // Only the first record has the field; the second and third mention its name in a value.
    QVector<Record> records = {
        makeRecord(kT, Level::Info, QStringLiteral("app"), QStringLiteral("one"),
                   QStringLiteral(R"({"provider":"x"})")),
        makeRecord(kT + 1, Level::Info, QStringLiteral("app"), QStringLiteral("two"),
                   QStringLiteral(R"({"note":"provider"})")),
        makeRecord(kT + 2, Level::Info, QStringLiteral("app"), QStringLiteral("three"),
                   QStringLiteral(R"({"note":"say \"provider\": 1"})")),
        makeRecord(kT + 3, Level::Info, QStringLiteral("app"), QStringLiteral("four")),
    };
    TestDatabase db;
    QVERIFY2(db.open(databasePath(), records), qPrintable(db.error()));
    LogStore& store = db.store();

    QCOMPARE(sortedIdsOf(store.findRecords(conditionFor(QStringLiteral("provider:*")), 10).value()),
             QList<qint64>{1});
    QCOMPARE(sortedIdsOf(store.findRecords(conditionFor(QStringLiteral("-provider:*")), 10).value()),
             (QList<qint64>{2, 3, 4}));
    QCOMPARE(sortedIdsOf(store.findRecords(conditionFor(QStringLiteral("provider:x")), 10).value()),
             QList<qint64>{1});
    QCOMPARE(sortedIdsOf(store.findRecords(conditionFor(QStringLiteral("note:provider")), 10).value()),
             QList<qint64>{2});

    const std::optional<QVector<StatsRow>> rows = store.fieldStatistics({}, QStringLiteral("provider"), 4);
    QVERIFY2(rows, qPrintable(store.errorString()));
    QCOMPARE(valuesOf(*rows), (QStringList{QStringLiteral("(no field)"), QStringLiteral("x")}));
    QCOMPARE(countsOf(*rows), (QList<qint64>{3, 1}));
}

void TestSearch::readTransactionKeepsOneState() {
    TestDatabase reader;
    QVERIFY2(reader.open(databasePath(), fixtureRecords()), qPrintable(reader.error()));
    LogStore& store = reader.store();

    QVERIFY2(store.beginRead(), qPrintable(store.errorString()));
    QCOMPARE(store.recordCount().value_or(-1), kFixtureSize);
    {
        // Another connection writes meanwhile; WAL lets it, and the reader keeps its state.
        TestDatabase writer;
        QVERIFY2(writer.open(databasePath(), {makeRecord(kT + 3 * kHour, Level::Error, QStringLiteral("cart"),
                                                         QStringLiteral("late record"))}),
                 qPrintable(writer.error()));
    }
    QCOMPARE(store.recordCount().value_or(-1), kFixtureSize);
    QCOMPARE(store.matchSpan({}).value().lastTs, kT + 2 * kHour);
    QCOMPARE(store.findRecords({}, 100).value().size(), kFixtureSize);
    store.endRead();

    QCOMPARE(store.recordCount().value_or(-1), kFixtureSize + 1);
    QCOMPARE(store.matchSpan({}).value().lastTs, kT + 3 * kHour);
}

void TestSearch::searchesMillionRecordsQuickly() {
    if (qEnvironmentVariable("LV_PERF") != QLatin1String("1")) {
        QSKIP("Set LV_PERF=1 to run the performance test on 1,000,000 records (about a minute).");
    }
    constexpr int kRecords = 1'000'000;
    constexpr int kBatch = 10'000;
    {
        Database database;
        QVERIFY2(database.open(databasePath()), qPrintable(database.errorString()));
        LogStore store(database);
        QRandomGenerator random(42);
        QElapsedTimer timer;
        timer.start();
        for (int first = 0; first < kRecords; first += kBatch) {
            QVector<Record> batch;
            batch.reserve(kBatch);
            for (int i = first; i < first + kBatch; ++i) {
                batch.append(generatedRecord(i, random));
            }
            QVERIFY2(store.insertBatch(batch), qPrintable(store.errorString()));
        }
        qInfo("perf: %d records written in %lld ms", kRecords, timer.elapsed());
    }

    // DESIGN.md section 1.4: a search usually takes at most a second on 1,000,000 records. Statistics over
    // all of them read every record twice (bars and groups), so they may take a little longer.
    constexpr qint64 kUsualMs = 1000;
    constexpr qint64 kStatisticsOfAllMs = 1500;
    struct Case {
        const char* name;
        SearchRequest request;
        qint64 limitMs = kUsualMs;
    };
    const qint64 newest = kT - 7 * kDay + qint64(kRecords - 1) * 600;
    QVector<Case> cases;
    cases.append({"all records", SearchRequest()});
    cases.append({"word: timeout", SearchRequest()});
    cases.back().request.text = QStringLiteral("timeout");
    cases.append({"field: provider:bepaid", SearchRequest()});
    cases.back().request.text = QStringLiteral("provider:bepaid");
    cases.append({"levels ERROR+FATAL, last 24 hours", SearchRequest()});
    cases.back().request.levels = {Level::Error, Level::Fatal};
    cases.back().request.from = newest + 1 - kDay;
    cases.back().request.to = newest + 1;
    cases.append({"word + field + exclusion", SearchRequest()});
    cases.back().request.text = QStringLiteral("timeout provider:bepaid -env:test");
    cases.append({"stats by similar messages, ERROR only", SearchRequest()});
    cases.back().request.levels = {Level::Error};
    cases.back().request.statsMode = StatsMode::Message;
    cases.append({"stats by similar messages, all records", SearchRequest(), kStatisticsOfAllMs});
    cases.back().request.statsMode = StatsMode::Message;
    cases.append({"stats by field provider, all records", SearchRequest(), kStatisticsOfAllMs});
    cases.back().request.statsMode = StatsMode::Field;
    cases.back().request.statsField = QStringLiteral("provider");

    SearchService service;
    service.setDatabasePath(databasePath());
    QSignalSpy finished(&service, &SearchService::finished);
    QStringList tooSlow;
    for (const Case& test : cases) {
        // The second run shows the usual time; the first one also warms the caches.
        for (int run = 0; run < 2; ++run) {
            finished.clear();
            service.search(test.request);
            QVERIFY(finished.wait(60'000));
            const SearchResult result = finished.first().first().value<SearchResult>();
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QVERIFY(result.total > 0);
            if (run == 1) {
                qInfo("perf: %-40s %5lld ms, %7lld matches", test.name, result.elapsedMs, result.total);
                if (result.elapsedMs > test.limitMs) {
                    tooSlow.append(
                        QStringLiteral("%1: %2 ms").arg(QLatin1String(test.name)).arg(result.elapsedMs));
                }
            }
        }
    }
    QVERIFY2(tooSlow.isEmpty(), qPrintable(tooSlow.join(QStringLiteral("; "))));
}
