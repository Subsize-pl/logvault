#include "Database.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>

namespace {

constexpr QLatin1StringView kDriver("QSQLITE");

// Settings that belong to the connection only, so they are safe to apply before the file is checked.
constexpr const char* kConnectionPragmas[] = {
    "PRAGMA busy_timeout = 5000",
    "PRAGMA foreign_keys = ON",
    "PRAGMA synchronous = NORMAL",
};

// DESIGN.md §4.2. Every statement is idempotent, so two connections creating one new file cannot conflict.
constexpr const char* kSchemaStatements[] = {
    R"(CREATE TABLE IF NOT EXISTS meta (
           key   TEXT PRIMARY KEY,
           value TEXT NOT NULL))",
    R"(CREATE TABLE IF NOT EXISTS sources (
           id         INTEGER PRIMARY KEY,
           name       TEXT    NOT NULL UNIQUE,
           created_at INTEGER NOT NULL))",
    R"(CREATE TABLE IF NOT EXISTS records (
           id        INTEGER PRIMARY KEY,
           ts        INTEGER NOT NULL,
           level     INTEGER NOT NULL,
           source_id INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE,
           message   TEXT    NOT NULL,
           msg_key   TEXT    NOT NULL,
           fields    TEXT,
           raw       TEXT    NOT NULL))",
    "CREATE INDEX IF NOT EXISTS idx_records_ts        ON records(ts)",
    "CREATE INDEX IF NOT EXISTS idx_records_source_ts ON records(source_id, ts)",
    "CREATE INDEX IF NOT EXISTS idx_records_level_ts  ON records(level, ts)",
};

// SQLite's result code for a file whose header is not an SQLite header (SQLITE_NOTADB).
constexpr QLatin1StringView kNotADatabaseCode("26");

// Runs one statement; on failure stores the driver's error.
bool execStatement(const QSqlDatabase& db, const QString& sql, QSqlError& error) {
    QSqlQuery query(db);
    if (query.exec(sql)) {
        return true;
    }
    error = query.lastError();
    return false;
}

} // namespace

Database::~Database() { closeConnection(); }

bool Database::open(const QString& path) {
    if (isOpen()) {
        return fail(tr("The database is already open."));
    }
    m_errorString.clear();
    if (openConnection(path)) {
        return true;
    }
    closeConnection();
    return false;
}

bool Database::isOpen() const { return m_db.isOpen(); }

QString Database::path() const { return m_db.databaseName(); }

QString Database::errorString() const { return m_errorString; }

QSqlDatabase Database::connection() const {
    Q_ASSERT(QThread::currentThread() == m_thread);
    return m_db;
}

bool Database::openConnection(const QString& path) {
    if (path.isEmpty()) {
        return fail(tr("No database file was given."));
    }
    const QString nativePath = QDir::toNativeSeparators(path);

    m_thread = QThread::currentThread();
    // Qt needs a distinct name for every connection. The object's address is unique among live objects, and
    // the connection is removed before the object dies.
    m_connectionName = QStringLiteral("logvault-%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_db = QSqlDatabase::addDatabase(kDriver, m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        return fail(tr("Cannot open %1: %2").arg(nativePath, m_db.lastError().text()));
    }

    QSqlError error;
    for (const char* pragma : kConnectionPragmas) {
        if (!execStatement(m_db, QString::fromLatin1(pragma), error)) {
            return failRead(error);
        }
    }

    // The file is checked before anything is written to it: journal_mode = WAL is stored in the file itself,
    // and a foreign or newer database must stay exactly as it was.
    const SchemaState state = inspectSchema();
    if (state == SchemaState::Invalid) {
        return false;
    }
    if (!enableWal()) {
        return false;
    }
    return state == SchemaState::Current || createSchema();
}

Database::SchemaState Database::inspectSchema() {
    const QString nativePath = QDir::toNativeSeparators(m_db.databaseName());
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'"))) {
        failRead(query.lastError());
        return SchemaState::Invalid;
    }
    QStringList tables;
    while (query.next()) {
        const QString name = query.value(0).toString();
        if (!name.startsWith(QLatin1StringView("sqlite_"))) {
            tables += name;
        }
    }
    if (query.lastError().isValid()) {
        failRead(query.lastError());
        return SchemaState::Invalid;
    }

    if (tables.isEmpty()) {
        return SchemaState::Empty;
    }
    if (!tables.contains(QLatin1StringView("meta"))) {
        fail(tr("%1 is not a LogVault database.").arg(nativePath));
        return SchemaState::Invalid;
    }

    int version = 0;
    if (!readSchemaVersion(version)) {
        return SchemaState::Invalid;
    }
    if (version > kSchemaVersion) {
        fail(tr("%1 was created by a newer version of LogVault (schema version %2, this version supports %3).")
                 .arg(nativePath)
                 .arg(version)
                 .arg(kSchemaVersion));
        return SchemaState::Invalid;
    }
    return SchemaState::Current;
}

bool Database::readSchemaVersion(int& version) {
    const QString nativePath = QDir::toNativeSeparators(m_db.databaseName());
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if (!query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"))) {
        return failRead(query.lastError());
    }
    query.addBindValue(QStringLiteral("schema_version"));
    if (!query.exec()) {
        return failRead(query.lastError());
    }
    if (!query.next()) {
        if (query.lastError().isValid()) {
            return failRead(query.lastError());
        }
        return fail(tr("%1 is not a LogVault database (it has no schema version).").arg(nativePath));
    }

    const QString text = query.value(0).toString();
    bool ok = false;
    version = text.toInt(&ok);
    if (!ok || version < 1) {
        return fail(tr("%1 is damaged: invalid schema version \"%2\".").arg(nativePath, text));
    }
    return true;
}

bool Database::enableWal() {
    const QString nativePath = QDir::toNativeSeparators(m_db.databaseName());
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !query.next()) {
        return fail(tr("Cannot switch %1 to WAL mode: %2").arg(nativePath, query.lastError().text()));
    }
    // SQLite keeps the old mode without an error where WAL is impossible, so the answer must be checked.
    const QString mode = query.value(0).toString();
    if (mode.compare(QLatin1StringView("wal"), Qt::CaseInsensitive) != 0) {
        return fail(tr("Cannot switch %1 to WAL mode (the journal mode stays \"%2\").").arg(nativePath, mode));
    }
    return true;
}

bool Database::createSchema() {
    const QString nativePath = QDir::toNativeSeparators(m_db.databaseName());
    QSqlError error;
    // IMMEDIATE takes the write lock at once, so a second connection creating the same file waits here.
    if (!execStatement(m_db, QStringLiteral("BEGIN IMMEDIATE"), error)) {
        return fail(tr("Cannot create the schema in %1: %2").arg(nativePath, error.text()));
    }

    bool ok = true;
    for (const char* statement : kSchemaStatements) {
        if (!execStatement(m_db, QString::fromLatin1(statement), error)) {
            ok = false;
            break;
        }
    }
    if (ok) {
        QSqlQuery query(m_db);
        ok = query.prepare(QStringLiteral("INSERT OR IGNORE INTO meta (key, value) VALUES (?, ?)"));
        if (ok) {
            query.addBindValue(QStringLiteral("schema_version"));
            query.addBindValue(QString::number(kSchemaVersion));
            ok = query.exec();
        }
        if (!ok) {
            error = query.lastError();
        }
    }
    if (ok && execStatement(m_db, QStringLiteral("COMMIT"), error)) {
        return true;
    }

    QSqlError rollbackError;
    if (!execStatement(m_db, QStringLiteral("ROLLBACK"), rollbackError)) {
        qWarning("Database: ROLLBACK failed: %s", qPrintable(rollbackError.text()));
    }
    return fail(tr("Cannot create the schema in %1: %2").arg(nativePath, error.text()));
}

bool Database::fail(const QString& message) {
    m_errorString = message;
    return false;
}

bool Database::failRead(const QSqlError& error) {
    const QString nativePath = QDir::toNativeSeparators(m_db.databaseName());
    // SQLite opens any file lazily; a file that is not SQLite at all fails on the first statement that reads it.
    if (error.nativeErrorCode() == kNotADatabaseCode) {
        return fail(tr("%1 is not a LogVault database.").arg(nativePath));
    }
    return fail(tr("Cannot read %1: %2").arg(nativePath, error.text()));
}

void Database::closeConnection() {
    if (m_connectionName.isEmpty()) {
        return;
    }
    Q_ASSERT(QThread::currentThread() == m_thread);
    m_db.close();
    // removeDatabase() complains and breaks the connection if a handle to it still exists, so drop ours first.
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
    m_thread = nullptr;
}
