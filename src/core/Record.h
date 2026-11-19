#pragma once

#include <QString>
#include <QtGlobal>

/// Severity of a record. The numeric values are stored in the database, so they must never change.
enum class Level : int { Unknown = 0, Trace, Debug, Info, Warn, Error, Fatal };

/// Number of levels, Level::Unknown to Level::Fatal.
inline constexpr int kLevelCount = 7;

/// One log record: the unit that is received, stored, searched and shown.
struct Record {
    qint64 id = 0;
    qint64 ts = 0; // milliseconds since 1970-01-01, UTC
    Level level = Level::Unknown;
    QString source;
    QString message;
    QString fields; // flat JSON object with the remaining fields; empty if none
    QString raw;    // original line (including joined stack trace lines)
};

/// Upper-case name of a level: "TRACE" ... "FATAL", or "UNKNOWN".
QString levelName(Level level);

/// Converts a value read from the database back to a Level; values out of range become Level::Unknown.
Level levelFromValue(int value);

/// Maximum length of a message key, in UTF-16 code units.
inline constexpr qsizetype kMessageKeyMaxLength = 200;

/// Groups similar messages: every run of digits (0-9) becomes '#' and the result is cut to
/// kMessageKeyMaxLength, e.g. "Timeout after 3000 ms" -> "Timeout after # ms".
QString messageKey(const QString& message);
