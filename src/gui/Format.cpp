#include "Format.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>

namespace Format {

QString count(qint64 value) {
    // The UI is in English, so numbers are written the English way whatever the system locale is.
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value);
}

QString timestamp(qint64 msecs) {
    return QDateTime::fromMSecsSinceEpoch(msecs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString dateTime(qint64 msecs) {
    return QDateTime::fromMSecsSinceEpoch(msecs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString duration(qint64 msecs) {
    if (msecs < 60'000) {
        // Hundredths matter for a search, tenths are enough for an import.
        const int decimals = msecs < 10'000 ? 2 : 1;
        return QCoreApplication::translate("Format", "%1 s")
            .arg(QString::number(double(msecs) / 1000.0, 'f', decimals));
    }
    const qint64 seconds = msecs / 1000;
    return QCoreApplication::translate("Format", "%1 min %2 s")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QChar(u'0'));
}

QString firstLine(const QString& text, qsizetype maxLength) {
    qsizetype end = text.indexOf(u'\n');
    if (end < 0) {
        end = text.size();
    }
    if (end > 0 && text.at(end - 1) == u'\r') {
        --end;
    }
    if (end > maxLength) {
        end = maxLength;
        // The two halves of a character outside the Basic Multilingual Plane stay together.
        if (end > 0 && text.at(end - 1).isHighSurrogate()) {
            --end;
        }
    }
    return text.left(end);
}

} // namespace Format
