#include "Record.h"

QString levelName(Level level) {
    switch (level) {
    case Level::Trace:
        return QStringLiteral("TRACE");
    case Level::Debug:
        return QStringLiteral("DEBUG");
    case Level::Info:
        return QStringLiteral("INFO");
    case Level::Warn:
        return QStringLiteral("WARN");
    case Level::Error:
        return QStringLiteral("ERROR");
    case Level::Fatal:
        return QStringLiteral("FATAL");
    case Level::Unknown:
        break;
    }
    return QStringLiteral("UNKNOWN");
}

Level levelFromValue(int value) {
    if (value < static_cast<int>(Level::Unknown) || value > static_cast<int>(Level::Fatal)) {
        return Level::Unknown;
    }
    return static_cast<Level>(value);
}

QString messageKey(const QString& message) {
    QString key;
    key.reserve(qMin(message.size(), kMessageKeyMaxLength + 1));
    bool inDigits = false;
    for (const QChar ch : message) {
        const bool isDigit = ch >= u'0' && ch <= u'9';
        if (!isDigit) {
            key += ch;
        } else if (!inDigits) {
            key += u'#';
        }
        inDigits = isDigit;
        // Messages can be long (stack traces); there is no need to scan past what will be cut off.
        if (key.size() > kMessageKeyMaxLength) {
            break;
        }
    }
    key.truncate(kMessageKeyMaxLength);
    // A character outside the BMP takes two code units; never keep half of it.
    if (!key.isEmpty() && key.back().isHighSurrogate()) {
        key.chop(1);
    }
    return key;
}
