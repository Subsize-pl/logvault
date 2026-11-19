#include "TimeParser.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

#include <cmath>
#include <iterator>

namespace {

constexpr qint64 kMsecsPerSecond = 1000;
constexpr qint64 kMsecsPerMinute = 60 * kMsecsPerSecond;
constexpr qint64 kMsecsPerHour = 60 * kMsecsPerMinute;
constexpr qint64 kMsecsPerDay = 24 * kMsecsPerHour;
constexpr qint64 kUnixEpochJulianDay = 2'440'588; // QDate(1970, 1, 1).toJulianDay()

bool isDigit(QChar ch) { return ch >= u'0' && ch <= u'9'; }

int digitValue(QChar ch) { return ch.unicode() - u'0'; }

// Reads a text from left to right. A read that does not match leaves the position where it was.
class Cursor {
public:
    explicit Cursor(QStringView text) : m_text(text) {}

    qsizetype position() const { return m_pos; }
    bool atEnd() const { return m_pos == m_text.size(); }

    // Exactly count digits, as a number.
    std::optional<int> number(qsizetype count) {
        if (m_text.size() - m_pos < count) {
            return std::nullopt;
        }
        int value = 0;
        for (qsizetype i = m_pos; i < m_pos + count; ++i) {
            if (!isDigit(m_text[i])) {
                return std::nullopt;
            }
            value = value * 10 + digitValue(m_text[i]);
        }
        m_pos += count;
        return value;
    }

    // All digits from the current position; empty if there are none.
    QStringView digits() {
        const qsizetype start = m_pos;
        while (m_pos < m_text.size() && isDigit(m_text[m_pos])) {
            ++m_pos;
        }
        return m_text.sliced(start, m_pos - start);
    }

    // The next character, if it is one of chars.
    std::optional<QChar> oneOf(QStringView chars) {
        if (m_pos < m_text.size() && chars.contains(m_text[m_pos])) {
            return m_text[m_pos++];
        }
        return std::nullopt;
    }

private:
    QStringView m_text;
    qsizetype m_pos = 0;
};

// The fixed part of a timestamp: a number of digits, then one of the separators (none for the seconds).
struct Field {
    qsizetype digits;
    QStringView separators;
};

constexpr Field kDateTimeFields[] = {
    {4, u"-"}, {2, u"-"}, {2, u"Tt "}, {2, u":"}, {2, u":"}, {2, u""},
};

// Milliseconds of a fraction of a second written as digits: "1" is 100, "123456" is 123.
int fractionMsecs(QStringView digits) {
    int msecs = 0;
    for (qsizetype i = 0; i < 3; ++i) {
        msecs = msecs * 10 + (i < digits.size() ? digitValue(digits[i]) : 0);
    }
    return msecs;
}

// Reads an optional zone: 'Z' or an offset from UTC. Returns false if a zone starts but is malformed;
// offsetMinutes stays empty when there is no zone.
bool readZone(Cursor& in, std::optional<int>& offsetMinutes) {
    if (in.oneOf(u"Zz")) {
        offsetMinutes = 0;
        return true;
    }
    const std::optional<QChar> sign = in.oneOf(u"+-");
    if (!sign) {
        return true;
    }
    const std::optional<int> hours = in.number(2);
    if (!hours) {
        return false;
    }
    std::optional<int> minutes = 0;
    if (in.oneOf(u":")) {
        minutes = in.number(2);
    } else if (const std::optional<int> joined = in.number(2)) {
        minutes = joined;
    }
    if (!minutes || *hours > 23 || *minutes > 59) {
        return false;
    }
    offsetMinutes = (*hours * 60 + *minutes) * (*sign == u'-' ? -1 : 1);
    return true;
}

// "1763560000" or "1763560000.123" (seconds), "1763560000123" (milliseconds).
std::optional<qint64> parseUnixTime(QStringView text) {
    Cursor in(text);
    const QStringView whole = in.digits();
    QStringView fraction;
    if (in.oneOf(u".")) {
        fraction = in.digits();
        if (fraction.isEmpty()) {
            return std::nullopt;
        }
    }
    if (!in.atEnd()) {
        return std::nullopt;
    }
    if (whole.size() == 10) {
        return whole.toLongLong() * kMsecsPerSecond + fractionMsecs(fraction);
    }
    if (whole.size() == 13 && fraction.isEmpty()) {
        return whole.toLongLong();
    }
    return std::nullopt;
}

} // namespace

std::optional<TimeParser::Match> TimeParser::parsePrefix(QStringView text) {
    Cursor in(text);
    int values[std::size(kDateTimeFields)] = {};
    for (std::size_t i = 0; i < std::size(kDateTimeFields); ++i) {
        const std::optional<int> value = in.number(kDateTimeFields[i].digits);
        if (!value ||
            (!kDateTimeFields[i].separators.isEmpty() && !in.oneOf(kDateTimeFields[i].separators))) {
            return std::nullopt;
        }
        values[i] = *value;
    }
    const auto [year, month, day, hour, minute, second] = values;

    int msecs = 0;
    if (in.oneOf(u".,")) {
        const QStringView fraction = in.digits();
        if (fraction.isEmpty()) {
            return std::nullopt;
        }
        msecs = fractionMsecs(fraction);
    }
    std::optional<int> offsetMinutes;
    if (!readZone(in, offsetMinutes)) {
        return std::nullopt;
    }

    const QDate date(year, month, day);
    if (!date.isValid() || hour > 23 || minute > 59 || second > 59) {
        return std::nullopt;
    }
    if (offsetMinutes) {
        // A fixed offset needs no time zone rules: count whole days, add the time, subtract the offset.
        const qint64 days = date.toJulianDay() - kUnixEpochJulianDay;
        const qint64 timeOfDay =
            hour * kMsecsPerHour + minute * kMsecsPerMinute + second * kMsecsPerSecond + msecs;
        return Match{days * kMsecsPerDay + timeOfDay - *offsetMinutes * kMsecsPerMinute, in.position()};
    }
    const QDateTime local(date, QTime(hour, minute, second, msecs), QTimeZone::LocalTime);
    if (!local.isValid()) {
        return std::nullopt;
    }
    return Match{local.toMSecsSinceEpoch(), in.position()};
}

std::optional<qint64> TimeParser::parse(QStringView text) {
    text = text.trimmed();
    if (const std::optional<Match> match = parsePrefix(text); match && match->length == text.size()) {
        return match->msecs;
    }
    return parseUnixTime(text);
}

std::optional<qint64> TimeParser::fromNumber(double value) {
    // Both ranges cover the years 2001-2286; NaN and infinities fail every comparison.
    if (value >= 1e9 && value < 1e10) {
        return std::llround(value * kMsecsPerSecond);
    }
    if (value >= 1e12 && value < 1e13) {
        return std::llround(value);
    }
    return std::nullopt;
}
