#pragma once

#include <QSqlDatabase>
#include <QString>

#include <utility>

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
