#include "tst_environment.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

namespace {

constexpr QLatin1StringView kSqliteDriver("QSQLITE");

/// Removes a database connection at the end of the scope, even when a failed check returns early.
/// Create it before the connection's QSqlDatabase and QSqlQuery handles: those must be destroyed first,
/// or Qt reports the connection as still in use.
class ConnectionGuard {
public:
    explicit ConnectionGuard(QString name) : m_name(std::move(name)) {}
    ~ConnectionGuard() { QSqlDatabase::removeDatabase(m_name); }

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    QString m_name;
};

} // namespace

void TestEnvironment::sqliteDriverIsAvailable() {
    const QString available = QSqlDatabase::drivers().join(QStringLiteral(", "));
    QVERIFY2(QSqlDatabase::isDriverAvailable(kSqliteDriver), qPrintable("Available drivers: " + available));
}

void TestEnvironment::jsonExtractReadsNestedField() {
    const QString connection = QStringLiteral("tst_environment_json");
    const ConnectionGuard guard(connection);
    QSqlDatabase db = QSqlDatabase::addDatabase(kSqliteDriver, connection);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(db.open(), qPrintable(db.lastError().text()));

    QSqlQuery query(db);
    QVERIFY2(query.exec(QStringLiteral(R"(SELECT json_extract('{"http":{"status":500}}', '$.http.status'))")),
             qPrintable(query.lastError().text()));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 500);
}

void TestEnvironment::walModeCanBeEnabled() {
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));

    // The guard comes after the directory, so the database file is closed before the directory is removed.
    const QString connection = QStringLiteral("tst_environment_wal");
    const ConnectionGuard guard(connection);
    QSqlDatabase db = QSqlDatabase::addDatabase(kSqliteDriver, connection);
    db.setDatabaseName(dir.filePath(QStringLiteral("environment.lvdb")));
    QVERIFY2(db.open(), qPrintable(db.lastError().text()));

    QSqlQuery query(db);
    QVERIFY2(query.exec(QStringLiteral("PRAGMA journal_mode = WAL")), qPrintable(query.lastError().text()));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("wal"));
}
