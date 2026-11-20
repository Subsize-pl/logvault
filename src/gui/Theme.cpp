#include "Theme.h"

#include <QFontDatabase>
#include <QGuiApplication>

#include <array>

using namespace Qt::StringLiterals;

namespace {

// Indexed by the Level value: UNKNOWN, TRACE, DEBUG, INFO, WARN, ERROR, FATAL.
constexpr std::array<QRgb, kLevelCount> kLevelColors = {0x9E9E9E, 0x8A8F98, 0x5B7083, 0x2E7D32,
                                                        0xE0A100, 0xD32F2F, 0x7B1FA2};

// Above this gray value a background is light enough for black text (WARN amber is, INFO green is not).
constexpr int kLightBackgroundGray = 150;

} // namespace

namespace Theme {

QColor levelColor(Level level) {
    const auto index = static_cast<std::size_t>(level);
    return QColor(index < kLevelColors.size() ? kLevelColors[index] : kLevelColors[0]);
}

QColor textColorOn(const QColor& background) {
    return qGray(background.rgb()) > kLightBackgroundGray ? QColor(Qt::black) : QColor(Qt::white);
}

QColor activeColor() { return levelColor(Level::Info); }

QColor inactiveColor() { return levelColor(Level::Unknown); }

QColor errorColor() { return levelColor(Level::Error); }

QFont monospaceFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    // Consolas comes with every supported Windows version and reads better than the default Courier New.
    if (QFontDatabase::hasFamily(u"Consolas"_s)) {
        font.setFamilies({u"Consolas"_s});
    }
    if (const qreal size = QGuiApplication::font().pointSizeF(); size > 0) {
        font.setPointSizeF(size);
    }
    return font;
}

} // namespace Theme
