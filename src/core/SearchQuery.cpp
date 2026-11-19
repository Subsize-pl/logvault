#include "SearchQuery.h"

#include <QStringList>

#include <algorithm>
#include <optional>

namespace {

constexpr qint64 kSecond = 1000;
constexpr qint64 kMinute = 60 * kSecond;
constexpr qint64 kHour = 60 * kMinute;
constexpr qint64 kDay = 24 * kHour;

// The bar widths of DESIGN.md section 6.4, finest first.
constexpr std::array<qint64, 12> kHistogramSteps = {kSecond,     5 * kSecond,  15 * kSecond, kMinute,
                                                    5 * kMinute, 15 * kMinute, kHour,        3 * kHour,
                                                    12 * kHour,  kDay,         7 * kDay,     30 * kDay};

// About 30,000 years either side of 1970: beyond any time a log line can carry, and small enough that the
// histogram arithmetic cannot overflow.
constexpr qint64 kTimeLimit = 1'000'000'000'000'000;

// The largest multiple of step that is not greater than value (also for times before 1970).
qint64 alignDown(qint64 value, qint64 step) {
    const qint64 remainder = value % step;
    return remainder < 0 ? value - remainder - step : value - remainder;
}

qint64 barsNeeded(qint64 first, qint64 last, qint64 step) {
    return (last - alignDown(first, step)) / step + 1;
}

// A piece of a search line part. Text typed inside quotes is taken literally: no field name, no wildcard.
struct Segment {
    QString text;
    bool quoted = false;
};

// One part of the search line, with its quotes removed.
using Part = QVector<Segment>;

// Splits the search line at whitespace outside quotes. Inside quotes, \" and \\ stand for " and \; a
// quote that is never closed runs to the end of the line.
QVector<Part> splitParts(QStringView line) {
    QVector<Part> parts;
    Part part;
    bool inPart = false; // a part can consist of quotes only, like ""
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        QChar ch = line[i];
        if (ch.isSpace() && !quoted) {
            if (inPart) {
                parts.append(part);
                part.clear();
                inPart = false;
            }
            continue;
        }
        inPart = true;
        if (ch == u'"') {
            quoted = !quoted;
            part.append({QString(), quoted});
            continue;
        }
        if (quoted && ch == u'\\' && i + 1 < line.size() && (line[i + 1] == u'"' || line[i + 1] == u'\\')) {
            ch = line[++i];
        }
        if (part.isEmpty() || part.back().quoted != quoted) {
            part.append({QString(), quoted});
        }
        part.back().text += ch;
    }
    if (inPart) {
        parts.append(part);
    }
    return parts;
}

// How one part of the search line is matched.
struct Term {
    enum class Kind { Text, FieldEquals, FieldLike, FieldExists };

    Kind kind = Kind::Text;
    bool excluded = false;
    QString field;
    QString value; // the text, the field value, or a LIKE pattern for FieldLike
};

QString joined(const QVector<Segment>& segments) {
    QString text;
    for (const Segment& segment : segments) {
        text += segment.text;
    }
    return text;
}

// Escapes the LIKE wildcards and the escape character itself; the conditions use ESCAPE '\'.
QString escapeLike(QStringView text) {
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar ch : text) {
        if (ch == u'%' || ch == u'_' || ch == u'\\') {
            escaped += u'\\';
        }
        escaped += ch;
    }
    return escaped;
}

bool hasWildcard(const QVector<Segment>& value) {
    return std::any_of(value.begin(), value.end(),
                       [](const Segment& segment) { return !segment.quoted && segment.text.contains(u'*'); });
}

// An unquoted * matches any characters; everything else is matched literally.
QString likePattern(const QVector<Segment>& value) {
    QString pattern;
    for (const Segment& segment : value) {
        if (segment.quoted) {
            pattern += escapeLike(segment.text);
            continue;
        }
        const QStringList pieces = segment.text.split(u'*');
        for (qsizetype i = 0; i < pieces.size(); ++i) {
            if (i > 0) {
                pattern += u'%';
            }
            pattern += escapeLike(pieces[i]);
        }
    }
    return pattern;
}

// Reads one part: a leading "-" excludes, "name:value" with a valid unquoted name matches a field, and
// anything else is text searched in the message. A part without any text, like "", gives no term.
std::optional<Term> interpret(Part part) {
    Term term;
    Segment& head = part.front();
    // A lone "-" is text, not an exclusion of nothing.
    if (!head.quoted && head.text.startsWith(u'-') && (head.text.size() > 1 || part.size() > 1)) {
        term.excluded = true;
        head.text.remove(0, 1);
    }

    const qsizetype colon = head.quoted ? -1 : head.text.indexOf(u':');
    if (colon > 0 && SearchQuery::isValidFieldName(QStringView(head.text).left(colon))) {
        QVector<Segment> value = part;
        value.front().text = head.text.mid(colon + 1);
        const bool hasQuotes =
            std::any_of(value.begin(), value.end(), [](const Segment& segment) { return segment.quoted; });
        // "name:" alone stays text, so a search for "Error:" finds messages that contain it.
        if (hasQuotes || !joined(value).isEmpty()) {
            term.field = head.text.left(colon);
            if (!hasWildcard(value)) {
                term.kind = Term::Kind::FieldEquals;
                term.value = joined(value);
            } else {
                term.value = likePattern(value);
                term.kind = term.value == u"%" ? Term::Kind::FieldExists : Term::Kind::FieldLike;
            }
            return term;
        }
    }

    term.value = joined(part);
    if (term.value.isEmpty()) {
        return std::nullopt;
    }
    return term;
}

QString fieldPath(const QString& name) { return QStringLiteral("$.\"%1\"").arg(name); }

QString placeholders(qsizetype count) { return QStringList(count, QStringLiteral("?")).join(u", "); }

// Collects conditions that are all combined with AND.
class ConditionBuilder {
public:
    void add(const QString& sql, const QVariantList& values = {}) {
        m_conditions.append(sql);
        m_values.append(values);
    }

    SqlFragment result() const { return {m_conditions.join(QStringLiteral(" AND ")), m_values}; }

private:
    QStringList m_conditions;
    QVariantList m_values;
};

void addLevelFilter(const QList<Level>& levels, ConditionBuilder& builder) {
    std::array<bool, kLevelCount> enabled{};
    for (const Level level : levels) {
        const int value = static_cast<int>(level);
        if (value >= 0 && value < kLevelCount) {
            enabled[value] = true;
        }
    }
    QVariantList values;
    for (int value = 0; value < kLevelCount; ++value) {
        if (enabled[value]) {
            values.append(value);
        }
    }
    if (values.size() == kLevelCount) {
        return;
    }
    if (values.isEmpty()) {
        // No level is enabled, so nothing can match.
        builder.add(QStringLiteral("0"));
        return;
    }
    builder.add(QStringLiteral("r.level IN (%1)").arg(placeholders(values.size())), values);
}

void addSourceFilter(const QList<qint64>& sourceIds, ConditionBuilder& builder) {
    if (sourceIds.isEmpty()) {
        return;
    }
    QVariantList values;
    for (const qint64 id : sourceIds) {
        values.append(id);
    }
    builder.add(QStringLiteral("r.source_id IN (%1)").arg(placeholders(values.size())), values);
}

void addPeriodFilter(qint64 from, qint64 to, ConditionBuilder& builder) {
    if (from != 0) {
        builder.add(QStringLiteral("r.ts >= ?"), {from});
    }
    if (to != 0) {
        builder.add(QStringLiteral("r.ts < ?"), {to});
    }
}

// A test of a field, after the quick mention check: SQLite then parses the JSON only of the records that
// can have the field.
SqlFragment fieldCondition(const QString& field, const QString& test, const QVariantList& values) {
    const SqlFragment mention = SearchQuery::fieldMention(field);
    return {mention.sql + QStringLiteral(" AND ") + test, mention.values + values};
}

void addTerm(const Term& term, ConditionBuilder& builder) {
    SqlFragment condition;
    switch (term.kind) {
    case Term::Kind::Text:
        condition = {QStringLiteral(R"(r.message LIKE ? ESCAPE '\')"),
                     {u'%' + escapeLike(term.value) + u'%'}};
        break;
    case Term::Kind::FieldEquals: {
        const SqlFragment value = SearchQuery::fieldValue(term.field);
        condition = fieldCondition(term.field, value.sql + QStringLiteral(" = ? COLLATE NOCASE"),
                                   value.values + QVariantList{term.value});
        break;
    }
    case Term::Kind::FieldLike: {
        const SqlFragment value = SearchQuery::fieldValue(term.field);
        condition = fieldCondition(term.field, value.sql + QStringLiteral(R"( LIKE ? ESCAPE '\')"),
                                   value.values + QVariantList{term.value});
        break;
    }
    case Term::Kind::FieldExists:
        condition = fieldCondition(term.field, QStringLiteral("json_extract(r.fields, ?) IS NOT NULL"),
                                   {fieldPath(term.field)});
        break;
    }
    if (term.excluded) {
        // A field test is false or NULL for a record without the field; such a record does not have the
        // excluded value, so it matches (as in Kibana).
        condition.sql = QStringLiteral("NOT coalesce(%1, 0)").arg(condition.sql);
    }
    builder.add(condition.sql, condition.values);
}

} // namespace

Histogram Histogram::forRange(qint64 first, qint64 last) {
    first = std::clamp(first, -kTimeLimit, kTimeLimit);
    last = std::clamp(last, first, kTimeLimit);
    const auto fits = [first, last](qint64 step) { return barsNeeded(first, last, step) <= kMaxBars; };

    const auto listed = std::find_if(kHistogramSteps.begin(), kHistogramSteps.end(), fits);
    qint64 step = listed != kHistogramSteps.end() ? *listed : kHistogramSteps.back();
    if (!fits(step)) {
        // More than 80 bars of 30 days (over six years): the smallest multiple of 30 days that fits.
        step *= (last - first) / (kMaxBars * step) + 1;
        while (!fits(step)) {
            step += kHistogramSteps.back();
        }
    }

    Histogram histogram;
    histogram.start = alignDown(first, step);
    histogram.step = step;
    histogram.bars.resize(barsNeeded(first, last, step));
    return histogram;
}

qint64 Histogram::total() const {
    qint64 total = 0;
    for (const LevelCounts& bar : bars) {
        for (const qint64 count : bar) {
            total += count;
        }
    }
    return total;
}

namespace SearchQuery {

SqlFragment build(const SearchRequest& request) {
    ConditionBuilder builder;
    addLevelFilter(request.levels, builder);
    addSourceFilter(request.sourceIds, builder);
    addPeriodFilter(request.from, request.to, builder);
    for (const Part& part : splitParts(request.text)) {
        if (const std::optional<Term> term = interpret(part)) {
            addTerm(*term, builder);
        }
    }
    return builder.result();
}

bool isValidFieldName(QStringView name) {
    return !name.isEmpty() && std::all_of(name.begin(), name.end(), [](QChar ch) {
        return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') || (ch >= u'0' && ch <= u'9') ||
               ch == u'_' || ch == u'.' || ch == u'@' || ch == u'-';
    });
}

SqlFragment fieldValue(const QString& name) {
    Q_ASSERT(isValidFieldName(name));
    // The path is bound like any value. json_extract() gives JSON true and false as 1 and 0, so they are
    // turned back into the words the record shows; CAST makes the number 500 and the string "500" equal.
    const QString path = fieldPath(name);
    return {QStringLiteral("CASE json_type(r.fields, ?) WHEN 'true' THEN 'true' WHEN 'false' THEN 'false' "
                           "ELSE CAST(json_extract(r.fields, ?) AS TEXT) END"),
            {path, path}};
}

SqlFragment fieldMention(const QString& name) {
    Q_ASSERT(isValidFieldName(name));
    // LogVault writes fields with QJsonDocument, which escapes none of the characters a valid name has, so
    // a record with the field always contains its name in quotes.
    return {QStringLiteral("instr(r.fields, ?) > 0"), {u'"' + name + u'"'}};
}

QString quote(const QString& text) {
    QString escaped = text;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'"', QStringLiteral("\\\""));
    return u'"' + escaped + u'"';
}

} // namespace SearchQuery
