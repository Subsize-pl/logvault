#pragma once

#include <QObject>

/// Checks the theme helpers: level colors made readable on light and dark backgrounds, the neutral
/// selection palette, and the toolbar glyphs drawn in the palette's colors.
class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void levelTextReadsOnLightAndDarkBackgrounds();
    void readableColorsStayAsTheyAre();
    void neutralPaletteFitsLightAndDarkThemes();
    void glyphIconsAreDrawn();
};
