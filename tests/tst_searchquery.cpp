#include "tst_searchquery.h"

#include "Record.h"
#include "SearchQuery.h"

#include <QList>
#include <QStringList>
#include <QTest>

namespace {

// The SQL that SearchQuery writes for each kind of part (DESIGN.md section 6.2). A field test comes after
// the quick check that the fields text mentions the name.
const QString kText = QStringLiteral(R"(r.message LIKE ? ESCAPE '\')");
const QString kMention = QStringLiteral("instr(r.fields, ?) > 0 AND ");
const QString kFieldValue =
    QStringLiteral("CASE json_type(r.fields, ?) WHEN 'true' THEN 'true' WHEN 'false' THEN 'false' "
                   "ELSE CAST(json_extract(r.fields, ?) AS TEXT) END");
const QString kEquals = kMention + kFieldValue + QStringLiteral(" = ? COLLATE NOCASE");
const QString kLike = kMention + kFieldValue + QStringLiteral(R"( LIKE ? ESCAPE '\')");
const QString kExists = kMention + QStringLiteral("json_extract(r.fields, ?) IS NOT NULL");

QString excluded(const QString& sql) { return QStringLiteral("NOT coalesce(%1, 0)").arg(sql); }

QString allOf(const QStringList& conditions) { return conditions.join(QStringLiteral(" AND ")); }

QString path(const QString& name) { return QStringLiteral("$.\"%1\"").arg(name); }

QString quoted(const QString& name) { return QStringLiteral("\"%1\"").arg(name); }

// The values of a field comparison: the quoted name, the JSON path twice, then the value or pattern.
QStringList fieldValues(const QString& name, const QString& value) {
    return {quoted(name), path(name), path(name), value};
}

QStringList existsValues(const QString& name) { return {quoted(name), path(name)}; }

QStringList asStrings(const QVariantList& values) {
    QStringList strings;
    for (const QVariant& value : values) {
        strings.append(value.toString());
    }
    return strings;
}

SqlFragment buildText(const QString& text) {
    SearchRequest request;
    request.text = text;
    return SearchQuery::build(request);
}

QList<Level> toLevels(const QList<int>& values) {
    QList<Level> levels;
    for (const int value : values) {
        levels.append(static_cast<Level>(value));
    }
    return levels;
}

} // namespace

void TestSearchQuery::searchLine_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("where");
    QTest::addColumn<QStringList>("values");

    // The examples of DESIGN.md section 6.2.
    QTest::newRow("word") << QStringLiteral("timeout") << kText << QStringList{QStringLiteral("%timeout%")};
    QTest::newRow("phrase") << QStringLiteral(R"("connection refused")") << kText
                            << QStringList{QStringLiteral("%connection refused%")};
    QTest::newRow("field equals") << QStringLiteral("provider:bepaid") << kEquals
                                  << fieldValues(QStringLiteral("provider"), QStringLiteral("bepaid"));
    QTest::newRow("field pattern") << QStringLiteral("user:adm*") << kLike
                                   << fieldValues(QStringLiteral("user"), QStringLiteral("adm%"));
    QTest::newRow("field exists") << QStringLiteral("order_id:*") << kExists
                                  << existsValues(QStringLiteral("order_id"));
    QTest::newRow("excluded word") << QStringLiteral("-debug") << excluded(kText)
                                   << QStringList{QStringLiteral("%debug%")};
    QTest::newRow("excluded field") << QStringLiteral("-env:test") << excluded(kEquals)
                                    << fieldValues(QStringLiteral("env"), QStringLiteral("test"));
    QTest::newRow("all combined with AND")
        << QStringLiteral("timeout provider:bepaid -env:test") << allOf({kText, kEquals, excluded(kEquals)})
        << QStringList{QStringLiteral("%timeout%")} +
               fieldValues(QStringLiteral("provider"), QStringLiteral("bepaid")) +
               fieldValues(QStringLiteral("env"), QStringLiteral("test"));

    // Empty input adds no condition.
    QTest::newRow("empty") << QString() << QString() << QStringList();
    QTest::newRow("blanks only") << QStringLiteral(" \t\n ") << QString() << QStringList();
    QTest::newRow("empty quotes") << QStringLiteral(R"("")") << QString() << QStringList();
    QTest::newRow("excluded empty quotes") << QStringLiteral(R"(-"")") << QString() << QStringList();
    QTest::newRow("blanks between parts") << QStringLiteral("  a \t b  ") << allOf({kText, kText})
                                          << QStringList{QStringLiteral("%a%"), QStringLiteral("%b%")};

    // Fields.
    QTest::newRow("nested field name") << QStringLiteral("http.status:500") << kEquals
                                       << fieldValues(QStringLiteral("http.status"), QStringLiteral("500"));
    QTest::newRow("name with @ and -")
        << QStringLiteral("@x-id:7") << kEquals << fieldValues(QStringLiteral("@x-id"), QStringLiteral("7"));
    QTest::newRow("only the first colon splits")
        << QStringLiteral("time:10:30") << kEquals
        << fieldValues(QStringLiteral("time"), QStringLiteral("10:30"));
    QTest::newRow("quoted value") << QStringLiteral(R"(agent:"Mozilla 5.0")") << kEquals
                                  << fieldValues(QStringLiteral("agent"), QStringLiteral("Mozilla 5.0"));
    QTest::newRow("empty quoted value")
        << QStringLiteral(R"(env:"")") << kEquals << fieldValues(QStringLiteral("env"), QString());
    QTest::newRow("quoted star is literal") << QStringLiteral(R"(path:"/api/*")") << kEquals
                                            << fieldValues(QStringLiteral("path"), QStringLiteral("/api/*"));
    QTest::newRow("stars around a quoted part")
        << QStringLiteral(R"(msg:*"50%_"*)") << kLike
        << fieldValues(QStringLiteral("msg"), QStringLiteral(R"(%50\%\_%)"));
    QTest::newRow("pattern escapes LIKE characters")
        << QStringLiteral("file:my_log*") << kLike
        << fieldValues(QStringLiteral("file"), QStringLiteral(R"(my\_log%)"));
    QTest::newRow("excluded pattern") << QStringLiteral("-user:adm*") << excluded(kLike)
                                      << fieldValues(QStringLiteral("user"), QStringLiteral("adm%"));
    QTest::newRow("excluded exists") << QStringLiteral("-order_id:*") << excluded(kExists)
                                     << existsValues(QStringLiteral("order_id"));
    QTest::newRow("excluded quoted value") << QStringLiteral(R"(-env:"qa 1")") << excluded(kEquals)
                                           << fieldValues(QStringLiteral("env"), QStringLiteral("qa 1"));

    // Text in the message.
    QTest::newRow("excluded phrase") << QStringLiteral(R"(-"connection refused")") << excluded(kText)
                                     << QStringList{QStringLiteral("%connection refused%")};
    QTest::newRow("quoted field syntax is text") << QStringLiteral(R"("provider:bepaid")") << kText
                                                 << QStringList{QStringLiteral("%provider:bepaid%")};
    QTest::newRow("name without value is text")
        << QStringLiteral("Error:") << kText << QStringList{QStringLiteral("%Error:%")};
    QTest::newRow("excluded name without value")
        << QStringLiteral("-Error:") << excluded(kText) << QStringList{QStringLiteral("%Error:%")};
    QTest::newRow("leading colon is text")
        << QStringLiteral(":value") << kText << QStringList{QStringLiteral("%:value%")};
    QTest::newRow("lone minus is text") << QStringLiteral("-") << kText << QStringList{QStringLiteral("%-%")};
    QTest::newRow("only one minus excludes")
        << QStringLiteral("--verbose") << excluded(kText) << QStringList{QStringLiteral("%-verbose%")};
    QTest::newRow("star in text is literal")
        << QStringLiteral("a*b") << kText << QStringList{QStringLiteral("%a*b%")};
    QTest::newRow("percent is escaped")
        << QStringLiteral("100%") << kText << QStringList{QStringLiteral(R"(%100\%%)")};
    QTest::newRow("underscore is escaped")
        << QStringLiteral("snake_case") << kText << QStringList{QStringLiteral(R"(%snake\_case%)")};
    QTest::newRow("backslash is escaped")
        << QStringLiteral(R"(logs\app)") << kText << QStringList{QStringLiteral(R"(%logs\\app%)")};
    // A drive letter is a valid field name; quotes make a path text.
    QTest::newRow("drive letter reads as a field")
        << QStringLiteral(R"(C:\temp)") << kEquals
        << fieldValues(QStringLiteral("C"), QStringLiteral(R"(\temp)"));
    QTest::newRow("adjacent quotes join one part")
        << QStringLiteral(R"(foo"bar baz"qux)") << kText << QStringList{QStringLiteral("%foobar bazqux%")};
    QTest::newRow("cyrillic") << QStringLiteral(u"\u043E\u0448\u0438\u0431\u043A\u0430") << kText
                              << QStringList{QStringLiteral(u"%\u043E\u0448\u0438\u0431\u043A\u0430%")};

    // Quotes that are not closed, and escapes inside quotes.
    QTest::newRow("unclosed quote runs to the end") << QStringLiteral(R"("connection refused)") << kText
                                                    << QStringList{QStringLiteral("%connection refused%")};
    QTest::newRow("unclosed quote after a word")
        << QStringLiteral(R"(timeout "no route  )") << allOf({kText, kText})
        << QStringList{QStringLiteral("%timeout%"), QStringLiteral("%no route  %")};
    QTest::newRow("unclosed quote in a field value")
        << QStringLiteral(R"(msg:"a b)") << kEquals
        << fieldValues(QStringLiteral("msg"), QStringLiteral("a b"));
    QTest::newRow("escaped quote inside quotes")
        << QStringLiteral(R"("say \"hi\"")") << kText << QStringList{QStringLiteral(R"(%say "hi"%)")};
    QTest::newRow("escaped backslash inside quotes")
        << QStringLiteral(R"("a\\b")") << kText << QStringList{QStringLiteral(R"(%a\\b%)")};
    QTest::newRow("other backslashes stay")
        << QStringLiteral(R"("C:\logs")") << kText << QStringList{QStringLiteral(R"(%C:\\logs%)")};
    QTest::newRow("backslash outside quotes is literal")
        << QStringLiteral(R"(a\"b c")") << kText << QStringList{QStringLiteral(R"(%a\\b c%)")};
}

void TestSearchQuery::searchLine() {
    QFETCH(QString, text);
    QFETCH(QString, where);
    QFETCH(QStringList, values);

    const SqlFragment condition = buildText(text);
    QCOMPARE(condition.sql, where);
    QCOMPARE(asStrings(condition.values), values);
    QCOMPARE(condition.sql.count(u'?'), condition.values.size());
}

void TestSearchQuery::injectionStaysInValues_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("where");
    QTest::addColumn<QStringList>("values");

    // Values: the SQL stays fixed and the input is a bound value.
    QTest::newRow("quotes in words") << QStringLiteral("';DROP TABLE records;--")
                                     << allOf({kText, kText, kText})
                                     << QStringList{QStringLiteral("%';DROP%"), QStringLiteral("%TABLE%"),
                                                    QStringLiteral("%records;--%")};
    QTest::newRow("quote in a phrase")
        << QStringLiteral(R"("x' OR '1'='1")") << kText << QStringList{QStringLiteral("%x' OR '1'='1%")};
    QTest::newRow("quote in a field value")
        << QStringLiteral(R"(provider:"x' OR '1'='1")") << kEquals
        << fieldValues(QStringLiteral("provider"), QStringLiteral("x' OR '1'='1"));
    QTest::newRow("SQL in a field value")
        << QStringLiteral("status:1);DELETE") << kEquals
        << fieldValues(QStringLiteral("status"), QStringLiteral("1);DELETE"));
    QTest::newRow("comment in a pattern") << QStringLiteral("user:*'--") << kLike
                                          << fieldValues(QStringLiteral("user"), QStringLiteral("%'--"));

    // Field names: a name with any other character makes the part plain text.
    QTest::newRow("quote in a field name")
        << QStringLiteral("a'b:1") << kText << QStringList{QStringLiteral("%a'b:1%")};
    QTest::newRow("SQL in a field name")
        << QStringLiteral("x');DROP/**/TABLE/**/records;--:1") << kText
        << QStringList{QStringLiteral("%x');DROP/**/TABLE/**/records;--:1%")};
    QTest::newRow("JSON path in a field name")
        << QStringLiteral("$.a:1") << kText << QStringList{QStringLiteral("%$.a:1%")};
    QTest::newRow("bracket in a field name")
        << QStringLiteral("a[0]:1") << kText << QStringList{QStringLiteral("%a[0]:1%")};
    QTest::newRow("double quote in a field name")
        << QStringLiteral(R"(a"b":1)") << kText << QStringList{QStringLiteral("%ab:1%")};
    QTest::newRow("non-ASCII field name") << QStringLiteral(u"\u0438\u043C\u044F:1") << kText
                                          << QStringList{QStringLiteral(u"%\u0438\u043C\u044F:1%")};
    // A valid name never reaches the SQL text either: it is part of the bound JSON path.
    QTest::newRow("valid name that looks like SQL")
        << QStringLiteral("OR:1") << kEquals << fieldValues(QStringLiteral("OR"), QStringLiteral("1"));
}

void TestSearchQuery::injectionStaysInValues() {
    QFETCH(QString, text);
    QFETCH(QString, where);
    QFETCH(QStringList, values);

    const SqlFragment condition = buildText(text);
    QCOMPARE(condition.sql, where);
    QCOMPARE(asStrings(condition.values), values);
    QCOMPARE(condition.sql.count(u'?'), condition.values.size());
}

void TestSearchQuery::levelFilter_data() {
    QTest::addColumn<QList<int>>("levels");
    QTest::addColumn<QString>("where");
    QTest::addColumn<QStringList>("values");

    QTest::newRow("all seven") << QList<int>{0, 1, 2, 3, 4, 5, 6} << QString() << QStringList();
    QTest::newRow("all seven, shuffled and repeated")
        << QList<int>{6, 5, 4, 3, 2, 1, 0, 3} << QString() << QStringList();
    QTest::newRow("none") << QList<int>() << QStringLiteral("0") << QStringList();
    QTest::newRow("one") << QList<int>{5} << QStringLiteral("r.level IN (?)")
                         << QStringList{QStringLiteral("5")};
    QTest::newRow("several, sorted and unique")
        << QList<int>{6, 5, 5, 0} << QStringLiteral("r.level IN (?, ?, ?)")
        << QStringList{QStringLiteral("0"), QStringLiteral("5"), QStringLiteral("6")};
    QTest::newRow("all but one") << QList<int>{0, 2, 3, 4, 5, 6}
                                 << QStringLiteral("r.level IN (?, ?, ?, ?, ?, ?)")
                                 << QStringList{QStringLiteral("0"), QStringLiteral("2"),
                                                QStringLiteral("3"), QStringLiteral("4"),
                                                QStringLiteral("5"), QStringLiteral("6")};
    QTest::newRow("values outside the scale are ignored")
        << QList<int>{42, -1} << QStringLiteral("0") << QStringList();
}

void TestSearchQuery::levelFilter() {
    QFETCH(QList<int>, levels);
    QFETCH(QString, where);
    QFETCH(QStringList, values);

    SearchRequest request;
    request.levels = toLevels(levels);
    const SqlFragment condition = SearchQuery::build(request);
    QCOMPARE(condition.sql, where);
    QCOMPARE(asStrings(condition.values), values);
}

void TestSearchQuery::sourceFilter() {
    SearchRequest request;
    QVERIFY(SearchQuery::build(request).sql.isEmpty());

    request.sourceIds = {12, 3};
    const SqlFragment condition = SearchQuery::build(request);
    QCOMPARE(condition.sql, QStringLiteral("r.source_id IN (?, ?)"));
    QCOMPARE(asStrings(condition.values), (QStringList{QStringLiteral("12"), QStringLiteral("3")}));
}

void TestSearchQuery::periodFilter() {
    SearchRequest request;
    request.from = 1'795'132'800'000;
    SqlFragment condition = SearchQuery::build(request);
    QCOMPARE(condition.sql, QStringLiteral("r.ts >= ?"));
    QCOMPARE(asStrings(condition.values), QStringList{QStringLiteral("1795132800000")});

    request.from = 0;
    request.to = 1'795'136'400'000;
    condition = SearchQuery::build(request);
    QCOMPARE(condition.sql, QStringLiteral("r.ts < ?"));
    QCOMPARE(asStrings(condition.values), QStringList{QStringLiteral("1795136400000")});

    // Only 0 means unbounded: a time before 1970 is a bound like any other.
    request.from = -5000;
    condition = SearchQuery::build(request);
    QCOMPARE(condition.sql, QStringLiteral("r.ts >= ? AND r.ts < ?"));
    QCOMPARE(asStrings(condition.values),
             (QStringList{QStringLiteral("-5000"), QStringLiteral("1795136400000")}));
}

void TestSearchQuery::filtersAndSearchLineCombine() {
    SearchRequest request;
    request.levels = {Level::Fatal, Level::Error};
    request.sourceIds = {7};
    request.from = 1000;
    request.to = 2000;
    request.text = QStringLiteral("timeout -env:test");
    SqlFragment condition = SearchQuery::build(request);
    QCOMPARE(condition.sql,
             allOf({QStringLiteral("r.level IN (?, ?)"), QStringLiteral("r.source_id IN (?)"),
                    QStringLiteral("r.ts >= ?"), QStringLiteral("r.ts < ?"), kText, excluded(kEquals)}));
    QCOMPARE(asStrings(condition.values),
             QStringList({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"),
                          QStringLiteral("1000"), QStringLiteral("2000"), QStringLiteral("%timeout%")}) +
                 fieldValues(QStringLiteral("env"), QStringLiteral("test")));
    QCOMPARE(condition.sql.count(u'?'), condition.values.size());

    // Without any enabled level nothing matches, whatever the search line says.
    request = SearchRequest();
    request.levels.clear();
    request.text = QStringLiteral("timeout");
    condition = SearchQuery::build(request);
    QCOMPARE(condition.sql, allOf({QStringLiteral("0"), kText}));
}

void TestSearchQuery::validatesFieldNames_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("valid");

    QTest::newRow("letters") << QStringLiteral("provider") << true;
    QTest::newRow("dotted") << QStringLiteral("http.status") << true;
    QTest::newRow("at sign") << QStringLiteral("@timestamp") << true;
    QTest::newRow("dash and underscore") << QStringLiteral("x-request_id") << true;
    QTest::newRow("digits") << QStringLiteral("10") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("space") << QStringLiteral("a b") << false;
    QTest::newRow("colon") << QStringLiteral("a:b") << false;
    QTest::newRow("double quote") << QStringLiteral(R"(a"b)") << false;
    QTest::newRow("single quote") << QStringLiteral("a'b") << false;
    QTest::newRow("backslash") << QStringLiteral(R"(a\b)") << false;
    QTest::newRow("dollar") << QStringLiteral("$.a") << false;
    QTest::newRow("brackets") << QStringLiteral("a[0]") << false;
    QTest::newRow("cyrillic") << QStringLiteral(u"\u0438\u043C\u044F") << false;
}

void TestSearchQuery::validatesFieldNames() {
    QFETCH(QString, name);
    QFETCH(bool, valid);
    QCOMPARE(SearchQuery::isValidFieldName(name), valid);
}

void TestSearchQuery::quoteRoundTrips_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("quoted");

    QTest::newRow("plain") << QStringLiteral("bepaid") << QStringLiteral(R"("bepaid")");
    QTest::newRow("empty") << QString() << QStringLiteral(R"("")");
    QTest::newRow("spaces") << QStringLiteral(" a  b ") << QStringLiteral(R"(" a  b ")");
    QTest::newRow("quotes") << QStringLiteral(R"(say "hi")") << QStringLiteral(R"("say \"hi\"")");
    QTest::newRow("backslashes") << QStringLiteral(R"(C:\temp\)") << QStringLiteral(R"("C:\\temp\\")");
    QTest::newRow("backslash before a quote") << QStringLiteral(R"(\")") << QStringLiteral(R"("\\\"")");
    QTest::newRow("search syntax") << QStringLiteral("-env:* x") << QStringLiteral(R"("-env:* x")");
    QTest::newRow("LIKE characters") << QStringLiteral("50%_") << QStringLiteral(R"("50%_")");
    QTest::newRow("cyrillic") << QStringLiteral(u"\u0436") << QStringLiteral(u"\"\u0436\"");
}

void TestSearchQuery::quoteRoundTrips() {
    QFETCH(QString, text);
    QFETCH(QString, quoted);
    QCOMPARE(SearchQuery::quote(text), quoted);

    // "Search for this value" and "Exclude this value" build parts like these, and they must match the
    // value exactly.
    SqlFragment condition = buildText(QStringLiteral("field:") + quoted);
    QCOMPARE(condition.sql, kEquals);
    QCOMPARE(asStrings(condition.values), fieldValues(QStringLiteral("field"), text));

    condition = buildText(QStringLiteral("-field:") + quoted);
    QCOMPARE(condition.sql, excluded(kEquals));
    QCOMPARE(asStrings(condition.values), fieldValues(QStringLiteral("field"), text));
}
