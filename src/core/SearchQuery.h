#pragma once

#include "Record.h"

#include <QList>
#include <QString>
#include <QStringView>
#include <QVariantList>
#include <QVector>

#include <array>

/// What the statistics group the matches by (DESIGN.md section 6.5).
enum class StatsMode { None, Level, Source, Message, Field };

/// A search as the UI sets it up: filters, the search line and what to compute (DESIGN.md section 6.1).
struct SearchRequest {
    // The enabled levels, all by default; an empty list matches nothing.
    QList<Level> levels = {Level::Unknown, Level::Trace, Level::Debug, Level::Info,
                           Level::Warn,    Level::Error, Level::Fatal};
    QList<qint64> sourceIds; // empty = all sources
    qint64 from = 0;         // ms UTC, inclusive; 0 = unbounded
    qint64 to = 0;           // ms UTC, exclusive; 0 = unbounded
    QString text;            // the search line (DESIGN.md section 6.2)
    int maxRows = 10000;
    StatsMode statsMode = StatsMode::None; // statistics are computed only while the Statistics tab is open
    QString statsField;                    // for StatsMode::Field
};

/// A piece of SQL and the values of its '?' placeholders, in order. Values never appear in the SQL text.
struct SqlFragment {
    QString sql;
    QVariantList values;
};

/// Record counts per level, indexed by the Level value.
using LevelCounts = std::array<qint64, kLevelCount>;

/// Record counts over time, stacked by level (DESIGN.md section 6.4).
struct Histogram {
    static constexpr int kMaxBars = 80;

    qint64 start = 0; // ms UTC, a multiple of step; bar i covers [start + i * step, start + (i + 1) * step)
    qint64 step = 0;  // ms; 0 when there are no bars
    QVector<LevelCounts> bars;

    /// Empty bars that cover the times first..last (inclusive). The step is the first of 1 s, 5 s, 15 s,
    /// 1 min, 5 min, 15 min, 1 h, 3 h, 12 h, 1 day, 7 days, 30 days that needs at most kMaxBars bars (a
    /// multiple of 30 days beyond that), and the start is aligned down to a multiple of the step.
    static Histogram forRange(qint64 first, qint64 last);

    /// The records in all bars.
    qint64 total() const;
};

/// One group of the statistics (DESIGN.md section 6.5).
struct StatsRow {
    QString value;        // level name, source name, message pattern, field value or "(no field)"
    qint64 count = 0;     // records in the group
    qint64 id = 0;        // the level value or the source id in those modes; 0 otherwise
    bool noField = false; // field mode: the group of records that do not have the field
};

/// Builds the condition of a search over the records table, aliased "r" (DESIGN.md section 6.2). Every
/// value is bound through a placeholder, so no input can change the SQL. Pure functions; safe in any thread.
namespace SearchQuery {

/// The condition for the request's level, source and period filters and its search line, all combined
/// with AND. An empty sql matches every record. Any search line is valid.
SqlFragment build(const SearchRequest& request);

/// Whether a name can be used as a field name: one or more of A-Z a-z 0-9 _ . @ -.
bool isValidFieldName(QStringView name);

/// The value of a field of r.fields as an SQL expression: strings and numbers as text, JSON true and false
/// as "true" and "false", NULL when the record does not have the field. The name must be valid.
SqlFragment fieldValue(const QString& name);

/// A quick test that every record with the field passes: r.fields contains the name in quotes. Put before
/// a test of fieldValue(), it saves parsing the JSON of most records. The name must be valid.
SqlFragment fieldMention(const QString& name);

/// The search line part that stands for exactly this text: in quotes, with " and \ escaped.
QString quote(const QString& text);

} // namespace SearchQuery
