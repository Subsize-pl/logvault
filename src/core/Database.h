#pragma once

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QString>

class QSqlError;
class QThread;

/// One SQLite connection to a LogVault database file, owned by the thread that opened it.
/// Applies the connection settings, creates or checks the schema, and closes the connection when destroyed.
class Database {
    Q_DECLARE_TR_FUNCTIONS(Database)

public:
    /// The schema version this build creates and understands (stored in meta.schema_version).
    static constexpr int kSchemaVersion = 1;

    Database() = default;
    /// Closes the connection. Call it in the opening thread, after every LogStore and query on it is gone.
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Opens the database file for the calling thread, creating it and the schema if the file is new or
    /// empty. A file of another program or of a newer LogVault is rejected without being modified. Returns
    /// false and sets errorString() on failure. Can succeed only once per object.
    bool open(const QString& path);

    bool isOpen() const;
    QString path() const;
    QString errorString() const;

    /// The underlying connection. Use it only in the thread that called open().
    QSqlDatabase connection() const;

private:
    enum class SchemaState { Empty, Current, Invalid };

    bool openConnection(const QString& path);
    SchemaState inspectSchema();
    bool readSchemaVersion(int& version);
    bool enableWal();
    bool createSchema();
    bool fail(const QString& message);
    bool failRead(const QSqlError& error);
    void closeConnection();

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_errorString;
    QThread* m_thread = nullptr;
};
