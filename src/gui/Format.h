#pragma once

#include <QString>
#include <QTimeZone>
#include <QtGlobal>

/// How numbers, times and durations are written in the UI (DESIGN.md section 2.4).
namespace Format {

/// A count with thousands separators: "12,345".
QString count(qint64 value);

/// A time in ms UTC as date and time with milliseconds in the zone: "2026-11-20 14:05:01.123".
QString timestamp(qint64 msecs, const QTimeZone& zone);

/// A time in ms UTC as date and time to the second in the zone: "2026-11-20 14:05:01".
QString dateTime(qint64 msecs, const QTimeZone& zone);

/// How the UI calls the display time zone: "UTC" or "local time".
QString timeZoneName(const QTimeZone& zone);

/// A duration: "0.18 s", "12.5 s", "3 min 05 s".
QString duration(qint64 msecs);

/// How much of a line the table and the details panel show; more would never be visible anyway.
inline constexpr qsizetype kMaxShownLength = 1000;

/// The first line of a text without its line break, cut to maxLength characters but never inside a
/// character: the table shows one line per record.
QString firstLine(const QString& text, qsizetype maxLength = kMaxShownLength);

} // namespace Format
