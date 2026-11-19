#pragma once

#include "Record.h"

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QStringView>

#include <optional>

/// A line as it was received or read from a file, before parsing.
struct ReceivedLine {
    QByteArray text;        // UTF-8, without the line break
    QString source;         // the record's source unless the line names its own
    qint64 receivedAt = 0;  // ms since 1970-01-01 UTC; the record's time if the line has none
    bool truncated = false; // the line was longer than the limit and was cut
};

/// Turns received lines into records by the rules of DESIGN.md section 5.4. Stateless; safe to use from
/// any thread.
namespace LineParser {

/// Parses a JSON object or a plain text line ("<time> <level> <message>"). Never fails: a line that is not
/// recognized becomes a record with the whole line as the message, Level::Unknown, the receive time and the
/// field "_parse_error": true; ok, if given, is then set to false. A truncated line gets "_truncated": true.
/// Record::raw is the line as received.
Record parse(const ReceivedLine& line, bool* ok = nullptr);

/// The level for a name or synonym, case-insensitive ("warning" -> Level::Warn); Level::Unknown otherwise.
Level levelFromName(QStringView name);

/// The text of a JSON string, number or boolean as records keep it and the search compares it: whole numbers
/// with every digit, booleans as "true" and "false". Nothing for null, objects and arrays.
std::optional<QString> scalarText(const QJsonValue& value);

} // namespace LineParser
