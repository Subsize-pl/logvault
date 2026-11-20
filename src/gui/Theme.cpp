#include "Theme.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QIconEngine>
#include <QList>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Indexed by the Level value: UNKNOWN, TRACE, DEBUG, INFO, WARN, ERROR, FATAL.
constexpr std::array<QRgb, kLevelCount> kLevelColors = {0x9E9E9E, 0x8A8F98, 0x5B7083, 0x2E7D32,
                                                        0xE0A100, 0xD32F2F, 0x7B1FA2};

// Above this gray value a background is light enough for black text (WARN amber is, INFO green is not).
constexpr int kLightBackgroundGray = 150;

// What WCAG AA asks of normal text.
constexpr double kMinTextContrast = 4.5;
// How far the lightness (0-255) moves per step while a color is made readable.
constexpr int kLightnessStep = 8;

// Slate grays for selected rows: no hue that a level color could clash with, and white or black text reads
// well on them.
constexpr QRgb kDarkHighlight = 0x4A5563;
constexpr QRgb kLightHighlight = 0xC8D3DF;
// How much alternate rows differ from the base: just enough to follow a row across the table.
constexpr float kDarkAlternateShare = 0.05F;
constexpr float kLightAlternateShare = 0.035F;
// Placeholder text halfway between the text and the base: clearly a hint, still readable.
constexpr float kPlaceholderShare = 0.45F;

// The glyphs are drawn in a box of this size, scaled to the icon.
constexpr qreal kGlyphBox = 16.0;

// The relative luminance of an sRGB color, as WCAG defines it.
double luminance(const QColor& color) {
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

// Dark when white text stands out more on it than black text does.
bool isDark(const QColor& background) {
    return Theme::contrast(Qt::white, background) > Theme::contrast(Qt::black, background);
}

// The color moved by share (0-1) of the way towards another one.
QColor mixed(const QColor& color, const QColor& other, float share) {
    return QColor::fromRgbF(color.redF() + (other.redF() - color.redF()) * share,
                            color.greenF() + (other.greenF() - color.greenF()) * share,
                            color.blueF() + (other.blueF() - color.blueF()) * share);
}

// An arrow into a tray (import) or out of it (export), in the glyph box.
void drawTrayArrow(QPainter& painter, const QColor& color, bool intoTray) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (intoTray) {
        painter.drawRect(QRectF(7, 1.5, 2, 6));
        painter.drawPolygon(QPolygonF(QList<QPointF>{{3.5, 7}, {12.5, 7}, {8, 11.5}}));
    } else {
        painter.drawRect(QRectF(7, 5.5, 2, 6));
        painter.drawPolygon(QPolygonF(QList<QPointF>{{3.5, 6}, {12.5, 6}, {8, 1.5}}));
    }
    QPen tray(color, 1.5);
    tray.setJoinStyle(Qt::MiterJoin);
    painter.setPen(tray);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(QPolygonF(QList<QPointF>{{2.5, 10}, {2.5, 14}, {13.5, 14}, {13.5, 10}}));
}

// Draws a glyph at any size. The colors come from the application palette when the icon is painted, so the
// icon follows the theme; standard icons are pixmaps drawn for light toolbars.
class GlyphIconEngine : public QIconEngine {
public:
    explicit GlyphIconEngine(Theme::Glyph glyph) : m_glyph(glyph) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2, rect.y() + (rect.height() - side) / 2);
        painter->scale(side / kGlyphBox, side / kGlyphBox);
        const QColor color = colorFor(mode, state);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        switch (m_glyph) {
        case Theme::Glyph::Import:
            drawTrayArrow(*painter, color, true);
            break;
        case Theme::Glyph::Export:
            drawTrayArrow(*painter, color, false);
            break;
        case Theme::Glyph::Start:
            painter->drawPolygon(QPolygonF(QList<QPointF>{{4.5, 2.5}, {13, 8}, {4.5, 13.5}}));
            break;
        case Theme::Glyph::Stop:
            painter->drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1, 1);
            break;
        case Theme::Glyph::Live:
            painter->drawEllipse(QPointF(8, 8), 4.5, 4.5);
            break;
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    // Drawn at the pixel size of the screen, so the glyph stays sharp at any display scaling.
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QIconEngine* clone() const override { return new GlyphIconEngine(m_glyph); }

private:
    QColor colorFor(QIcon::Mode mode, QIcon::State state) const {
        const QPalette palette = QGuiApplication::palette();
        if (mode == QIcon::Disabled) {
            return palette.color(QPalette::Disabled, QPalette::WindowText);
        }
        const QColor window = palette.color(QPalette::Window);
        switch (m_glyph) {
        case Theme::Glyph::Start:
            return Theme::readableOn(Theme::activeColor(), window);
        case Theme::Glyph::Stop:
            return Theme::readableOn(Theme::errorColor(), window);
        case Theme::Glyph::Live:
            return Theme::readableOn(state == QIcon::On ? Theme::activeColor() : Theme::inactiveColor(),
                                     window);
        case Theme::Glyph::Import:
        case Theme::Glyph::Export:
            break;
        }
        return palette.color(QPalette::WindowText);
    }

    Theme::Glyph m_glyph;
};

} // namespace

namespace Theme {

QColor levelColor(Level level) {
    const auto index = static_cast<std::size_t>(level);
    return QColor(index < kLevelColors.size() ? kLevelColors[index] : kLevelColors[0]);
}

QColor levelTextColor(Level level, const QColor& background) {
    return readableOn(levelColor(level), background);
}

QColor readableOn(const QColor& color, const QColor& background) {
    if (contrast(color, background) >= kMinTextContrast) {
        return color;
    }
    // The hue stays, so a level keeps its color; only the lightness moves away from the background's.
    const bool lighten = isDark(background);
    QColor result = color.toHsl();
    while (contrast(result, background) < kMinTextContrast) {
        const int lightness = result.lightness();
        const int next =
            lighten ? qMin(255, lightness + kLightnessStep) : qMax(0, lightness - kLightnessStep);
        if (next == lightness) {
            break; // white or black: no color has more contrast
        }
        result.setHsl(result.hslHue(), result.hslSaturation(), next, result.alpha());
    }
    return result.toRgb();
}

double contrast(const QColor& first, const QColor& second) {
    const double a = luminance(first);
    const double b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
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

QPalette neutralPalette(const QPalette& system) {
    QPalette palette = system;
    const QColor base = system.color(QPalette::Active, QPalette::Base);
    const bool dark = isDark(base);
    palette.setColor(QPalette::AlternateBase, dark ? mixed(base, Qt::white, kDarkAlternateShare)
                                                   : mixed(base, Qt::black, kLightAlternateShare));
    palette.setColor(QPalette::Highlight, QColor(dark ? kDarkHighlight : kLightHighlight));
    palette.setColor(QPalette::HighlightedText, dark ? QColor(Qt::white) : QColor(Qt::black));
    // The dark Windows palette gives hints the color of the text itself, so an empty field looks filled in.
    palette.setColor(QPalette::PlaceholderText,
                     mixed(system.color(QPalette::Active, QPalette::Text), base, kPlaceholderShare));
    return palette;
}

QIcon glyphIcon(Glyph glyph) {
    // QIcon takes over the engine.
    return QIcon(new GlyphIconEngine(glyph));
}

} // namespace Theme
