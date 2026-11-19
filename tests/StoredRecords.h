#pragma once

#include "Database.h"
#include "Record.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVector>

/// Reads every record of a database file in the order it was written, through a connection of its own.
/// Returns an empty list and sets *error if the file cannot be read.
inline QVector<Record> storedRecords(const QString& path, QString* error = nullptr) {
    Database database;
    if (!database.open(path)) {
        if (error != nullptr) {
            *error = database.errorString();
        }
        return {};
    }
    // Declared after the database, so the query is gone before the connection closes.
    QSqlQuery query(database.connection());
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral("SELECT r.id, r.ts, r.level, s.name, r.message, r.fields, r.raw "
                                   "FROM records r JOIN sources s ON s.id = r.source_id ORDER BY r.id"))) {
        if (error != nullptr) {
            *error = query.lastError().text();
        }
        return {};
    }
    QVector<Record> records;
    while (query.next()) {
        Record record;
        record.id = query.value(0).toLongLong();
        record.ts = query.value(1).toLongLong();
        record.level = levelFromValue(query.value(2).toInt());
        record.source = query.value(3).toString();
        record.message = query.value(4).toString();
        record.fields = query.value(5).toString();
        record.raw = query.value(6).toString();
        records.append(record);
    }
    return records;
}
