#include "tst_theme.h"

#include "Theme.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QTest>

#include <cstdlib>

namespace {

const QColor kWhite(0xFF, 0xFF, 0xFF);
const QColor kLightGray(0xF0, 0xF0, 0xF0);
const QColor kDarkGray(0x2D, 0x2D, 0x2D);
const QColor kNearBlack(0x1E, 0x1E, 0x1E);

constexpr double kMinTextContrast = 4.5;

// A palette like the one Windows gives Qt in its light or dark theme, with an accent color selection.
QPalette systemPalette(bool dark) {
    QPalette palette;
    palette.setColor(QPalette::Window, dark ? QColor(0x20, 0x20, 0x20) : kLightGray);
    palette.setColor(QPalette::Base, dark ? kNearBlack : kWhite);
    palette.setColor(QPalette::Text, dark ? kWhite : QColor(Qt::black));
    palette.setColor(QPalette::PlaceholderText, palette.color(QPalette::Text));
    palette.setColor(QPalette::AlternateBase, QColor(0x5A, 0x2D, 0x7A));
    palette.setColor(QPalette::Highlight, QColor(0x8A, 0x3F, 0xC4));
    palette.setColor(QPalette::HighlightedText, kWhite);
    return palette;
}

// How colorful a color is: 0 for grays. Unlike the HSL saturation, it stays small for light tints.
int chroma(const QColor& color) {
    return qMax(color.red(), qMax(color.green(), color.blue())) -
           qMin(color.red(), qMin(color.green(), color.blue()));
}

// The pixels of an icon that are not transparent, and the color of the one in the middle.
struct Drawn {
    int opaquePixels = 0;
    QColor center;
};

Drawn drawn(const QIcon& icon, QIcon::Mode mode = QIcon::Normal, QIcon::State state = QIcon::Off) {
    const QImage image = icon.pixmap(QSize(32, 32), 1.0, mode, state).toImage();
    Drawn result;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                ++result.opaquePixels;
            }
        }
    }
    result.center = image.pixelColor(image.width() / 2, image.height() / 2);
    return result;
}

} // namespace

void TestTheme::levelTextReadsOnLightAndDarkBackgrounds() {
    for (const QColor& background : {kWhite, kLightGray, kDarkGray, kNearBlack}) {
        for (int value = 0; value < kLevelCount; ++value) {
            const Level level = levelFromValue(value);
            const QColor text = Theme::levelTextColor(level, background);
            const QString what = levelName(level) + u" on " + background.name();
            QVERIFY2(Theme::contrast(text, background) >= kMinTextContrast, qPrintable(what));
            // The level keeps its hue (gray levels have none).
            const QColor original = Theme::levelColor(level);
            if (original.hslSaturation() > 40) {
                QVERIFY2(std::abs(text.hslHue() - original.hslHue()) <= 4, qPrintable(what));
            }
        }
    }
    // The colors that were too dim on a dark background got lighter, the bright ones on white darker.
    QVERIFY(Theme::levelTextColor(Level::Fatal, kNearBlack).lightness() >
            Theme::levelColor(Level::Fatal).lightness());
    QVERIFY(Theme::levelTextColor(Level::Warn, kWhite).lightness() <
            Theme::levelColor(Level::Warn).lightness());
}

void TestTheme::readableColorsStayAsTheyAre() {
    QCOMPARE(Theme::readableOn(QColor(Qt::black), kWhite), QColor(Qt::black));
    QCOMPARE(Theme::levelTextColor(Level::Fatal, kWhite), Theme::levelColor(Level::Fatal));
    QCOMPARE(Theme::levelTextColor(Level::Warn, kNearBlack), Theme::levelColor(Level::Warn));
    QCOMPARE(Theme::contrast(kWhite, QColor(Qt::black)), 21.0);
    QCOMPARE(Theme::contrast(kDarkGray, kDarkGray), 1.0);
}

void TestTheme::neutralPaletteFitsLightAndDarkThemes() {
    for (const bool dark : {false, true}) {
        const QPalette system = systemPalette(dark);
        const QPalette palette = Theme::neutralPalette(system);
        const QColor base = palette.color(QPalette::Base);
        QCOMPARE(base, system.color(QPalette::Base));

        // A gray selection instead of the accent color, with text that reads on it.
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
            const QColor highlight = palette.color(group, QPalette::Highlight);
            QVERIFY2(chroma(highlight) < 40, qPrintable(highlight.name()));
            QVERIFY(Theme::contrast(palette.color(group, QPalette::HighlightedText), highlight) >=
                    kMinTextContrast);
        }

        // Alternate rows close to the base, so the level colors keep reading on them.
        const QColor alternate = palette.color(QPalette::AlternateBase);
        QVERIFY(alternate != base);
        QVERIFY2(chroma(alternate) < 10, qPrintable(alternate.name()));
        QVERIFY(Theme::contrast(alternate, base) < 1.3);
        for (int value = 0; value < kLevelCount; ++value) {
            const QColor text = Theme::levelTextColor(levelFromValue(value), base);
            QVERIFY2(Theme::contrast(text, alternate) >= 3.5, qPrintable(levelName(levelFromValue(value))));
        }

        // Hints are neither the text color nor invisible.
        const QColor placeholder = palette.color(QPalette::PlaceholderText);
        QVERIFY(placeholder != palette.color(QPalette::Text));
        QVERIFY(Theme::contrast(placeholder, base) >= 2.5);
        QVERIFY(Theme::contrast(placeholder, base) < Theme::contrast(palette.color(QPalette::Text), base));
    }
}

void TestTheme::glyphIconsAreDrawn() {
    for (const Theme::Glyph glyph : {Theme::Glyph::Import, Theme::Glyph::Export, Theme::Glyph::Start,
                                     Theme::Glyph::Stop, Theme::Glyph::Live}) {
        const QIcon icon = Theme::glyphIcon(glyph);
        QVERIFY(!icon.isNull());
        const Drawn normal = drawn(icon);
        // Something is drawn, but not the whole square.
        QVERIFY(normal.opaquePixels > 32 * 32 / 10);
        QVERIFY(normal.opaquePixels < 32 * 32);
    }
    // Live is green when on and gray when off; a disabled icon is drawn in another color.
    const QIcon live = Theme::glyphIcon(Theme::Glyph::Live);
    const QColor on = drawn(live, QIcon::Normal, QIcon::On).center;
    const QColor off = drawn(live, QIcon::Normal, QIcon::Off).center;
    QVERIFY(on.hslSaturation() > off.hslSaturation());
    QVERIFY(on.green() > on.red());
    const QIcon stop = Theme::glyphIcon(Theme::Glyph::Stop);
    QVERIFY(drawn(stop).center != drawn(stop, QIcon::Disabled).center);
    // Sharp on scaled displays: drawn at the pixel size.
    const QPixmap scaled = stop.pixmap(QSize(16, 16), 2.0);
    QCOMPARE(scaled.size(), QSize(32, 32));
}
