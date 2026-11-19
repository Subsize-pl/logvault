#pragma once

#include <QStringView>
#include <QtGlobal>

#include <optional>

/// Parses the timestamps of DESIGN.md section 5.4 into milliseconds since 1970-01-01 UTC.
/// Stateless; safe to use from any thread.
namespace TimeParser {

/// A timestamp found at the start of a text.
struct Match {
    qint64 msecs = 0;     // milliseconds since 1970-01-01, UTC
    qsizetype length = 0; // characters the timestamp takes
};

/// Reads a date and time at the start of text: "yyyy-MM-dd", 'T' or a space, "HH:mm:ss", an optional
/// fraction after '.' or ',' (digits beyond milliseconds are ignored) and an optional zone: 'Z', "+03:00",
/// "+0300" or "+03". Without a zone the time is local. What follows the timestamp is not examined.
std::optional<Match> parsePrefix(QStringView text);

/// Parses a whole text (surrounding spaces allowed): a date and time as in parsePrefix(), or Unix time
/// written as 10 digits of seconds (a fraction is allowed) or 13 digits of milliseconds.
std::optional<qint64> parse(QStringView text);

/// Interprets a JSON number as Unix time: 10 integer digits are seconds, 13 are milliseconds.
std::optional<qint64> fromNumber(double value);

} // namespace TimeParser
