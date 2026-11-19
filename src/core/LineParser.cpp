#include "LineParser.h"

#include "TimeParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocale>

#include <array>
#include <limits>
#include <optional>
#include <span>

using namespace Qt::StringLiterals;

namespace {

// Names of the special JSON fields by priority (DESIGN.md section 5.4), compared case-insensitively.
constexpr std::array kTimeNames = {"time"_L1, "timestamp"_L1, "ts"_L1, "@timestamp"_L1};
constexpr std::array kLevelNames = {"level"_L1, "severity"_L1, "lvl"_L1};
constexpr std::array kMessageNames = {"message"_L1, "msg"_L1};
constexpr std::array kSourceNames = {"source"_L1, "service"_L1, "app"_L1};

constexpr auto kParseErrorField = "_parse_error"_L1;
constexpr auto kTruncatedField = "_truncated"_L1;

struct LevelSynonym {
    QLatin1StringView name;
    Level level;
};

constexpr LevelSynonym kLevelSynonyms[] = {
    {"trace"_L1, Level::Trace}, {"debug"_L1, Level::Debug},      {"dbg"_L1, Level::Debug},
    {"info"_L1, Level::Info},   {"information"_L1, Level::Info}, {"notice"_L1, Level::Info},
    {"warn"_L1, Level::Warn},   {"warning"_L1, Level::Warn},     {"error"_L1, Level::Error},
    {"err"_L1, Level::Error},   {"fatal"_L1, Level::Fatal},      {"critical"_L1, Level::Fatal},
    {"crit"_L1, Level::Fatal},  {"panic"_L1, Level::Fatal},
};

// How far into a line a bracketed level is looked for: room for "[information]" with some padding.
constexpr qsizetype kMaxBracketedLevelLength = 20;

bool isAsciiLetter(QChar ch) { return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z'); }

QStringView skipSpaces(QStringView text) {
    qsizetype i = 0;
    while (i < text.size() && text[i].isSpace()) {
        ++i;
    }
    return text.sliced(i);
}

// The object's key that matches the first possible name, compared case-insensitively.
std::optional<QString> findKey(const QJsonObject& object, std::span<const QLatin1StringView> names) {
    for (const QLatin1StringView name : names) {
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
                return it.key();
            }
        }
    }
    return std::nullopt;
}

// Removes the special field found by names from the object if use() accepts its value. A value that cannot
// be used (say, a time that does not parse) stays among the other fields, so nothing is lost.
template <typename Use>
bool takeField(QJsonObject& object, std::span<const QLatin1StringView> names, Use use) {
    const std::optional<QString> key = findKey(object, names);
    if (!key || !use(object.value(*key))) {
        return false;
    }
    object.remove(*key);
    return true;
}

// A JSON number as text. Whole numbers keep every digit: an id may not fit a double exactly.
QString numberText(const QJsonValue& value) {
    constexpr qint64 kNotWhole = std::numeric_limits<qint64>::min();
    const qint64 whole = value.toInteger(kNotWhole);
    if (whole != kNotWhole) {
        return QString::number(whole);
    }
    return QString::number(value.toDouble(), 'g', QLocale::FloatingPointShortest);
}

// The text of a string, number or boolean; nothing for null, objects and arrays.
std::optional<QString> scalarText(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Double:
        return numberText(value);
    case QJsonValue::Bool:
        return value.toBool() ? u"true"_s : u"false"_s;
    default:
        return std::nullopt;
    }
}

// Copies the object into fields, naming nested values with dots ({"http":{"status":500}} gives
// "http.status"); arrays are kept as JSON text, so every field holds a single value.
void flatten(const QJsonObject& object, const QString& prefix, QJsonObject& fields) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString name = prefix.isEmpty() ? it.key() : prefix + u'.' + it.key();
        const QJsonValue value = it.value();
        if (value.isObject()) {
            flatten(value.toObject(), name, fields);
        } else if (value.isArray()) {
            fields.insert(name,
                          QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)));
        } else {
            fields.insert(name, value);
        }
    }
}

bool parseJson(const QByteArray& line, QStringView text, Record& record, QJsonObject& fields) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    QJsonObject object = document.object();

    takeField(object, kTimeNames, [&record](const QJsonValue& value) {
        const std::optional<qint64> ts = value.isString()   ? TimeParser::parse(value.toString())
                                         : value.isDouble() ? TimeParser::fromNumber(value.toDouble())
                                                            : std::nullopt;
        record.ts = ts.value_or(record.ts);
        return ts.has_value();
    });
    takeField(object, kLevelNames, [&record](const QJsonValue& value) {
        record.level = LineParser::levelFromName(value.toString().trimmed());
        return record.level != Level::Unknown;
    });
    const bool hasMessage = takeField(object, kMessageNames, [&record](const QJsonValue& value) {
        const std::optional<QString> message = scalarText(value);
        record.message = message.value_or(QString());
        return message.has_value();
    });
    takeField(object, kSourceNames, [&record](const QJsonValue& value) {
        const QString name = scalarText(value).value_or(QString()).trimmed();
        if (!name.isEmpty()) {
            record.source = name;
        }
        return !name.isEmpty();
    });
    // Without a usable message field the whole line is the message, so the table never shows an empty row.
    if (!hasMessage) {
        record.message = text.toString();
    }
    flatten(object, QString(), fields);
    return true;
}

// A timestamp at the start of a line, bare or in brackets; a bare one must be followed by a space.
std::optional<TimeParser::Match> timestampAtStart(QStringView text) {
    if (text.startsWith(u'[')) {
        const std::optional<TimeParser::Match> match = TimeParser::parsePrefix(text.sliced(1));
        if (match && text.sliced(1 + match->length).startsWith(u']')) {
            return TimeParser::Match{match->msecs, match->length + 2};
        }
        return std::nullopt;
    }
    const std::optional<TimeParser::Match> match = TimeParser::parsePrefix(text);
    if (match && (match->length == text.size() || text[match->length].isSpace())) {
        return match;
    }
    return std::nullopt;
}

struct LevelMatch {
    Level level;
    qsizetype length;
};

// A level at the start of text: "[ERROR]", "ERROR:" or a bare "ERROR" followed by a space.
std::optional<LevelMatch> levelAtStart(QStringView text) {
    if (text.startsWith(u'[')) {
        const qsizetype close = text.first(qMin(text.size(), kMaxBracketedLevelLength)).indexOf(u']');
        if (close < 0) {
            return std::nullopt;
        }
        const Level level = LineParser::levelFromName(text.sliced(1, close - 1).trimmed());
        return level == Level::Unknown ? std::nullopt : std::optional(LevelMatch{level, close + 1});
    }
    qsizetype end = 0;
    while (end < text.size() && isAsciiLetter(text[end])) {
        ++end;
    }
    const Level level = LineParser::levelFromName(text.first(end));
    if (level == Level::Unknown) {
        return std::nullopt;
    }
    if (end == text.size() || text[end].isSpace()) {
        return LevelMatch{level, end};
    }
    if (text[end] == u':') {
        return LevelMatch{level, end + 1};
    }
    return std::nullopt;
}

// "<time> <level> <message>", where the time and the level are optional but not both.
bool parsePlainText(QStringView text, Record& record) {
    const std::optional<TimeParser::Match> stamp = timestampAtStart(text);
    if (stamp) {
        text = skipSpaces(text.sliced(stamp->length));
    }
    const std::optional<LevelMatch> level = levelAtStart(text);
    if (level) {
        text = skipSpaces(text.sliced(level->length));
    }
    if (!stamp && !level) {
        return false;
    }
    if (stamp) {
        record.ts = stamp->msecs;
    }
    if (level) {
        record.level = level->level;
    }
    record.message = text.toString();
    return true;
}

} // namespace

Record LineParser::parse(const ReceivedLine& line, bool* ok) {
    Record record;
    record.ts = line.receivedAt;
    record.source = line.source;
    record.raw = QString::fromUtf8(line.text);
    const QStringView text = QStringView(record.raw).trimmed();

    QJsonObject fields;
    const bool recognized =
        text.startsWith(u'{') ? parseJson(line.text, text, record, fields) : parsePlainText(text, record);
    if (!recognized) {
        // Nothing failed half-way: both parsers change the record only on success.
        record.message = text.toString();
        fields.insert(kParseErrorField, true);
    }
    if (line.truncated) {
        fields.insert(kTruncatedField, true);
    }
    if (!fields.isEmpty()) {
        record.fields = QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact));
    }
    if (ok != nullptr) {
        *ok = recognized;
    }
    return record;
}

Level LineParser::levelFromName(QStringView name) {
    for (const LevelSynonym& synonym : kLevelSynonyms) {
        if (name.compare(synonym.name, Qt::CaseInsensitive) == 0) {
            return synonym.level;
        }
    }
    return Level::Unknown;
}
