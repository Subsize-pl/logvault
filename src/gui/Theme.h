#pragma once

#include "Record.h"

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPalette>

/// Colors, fonts and icons shared by the widgets (DESIGN.md section 2.4).
namespace Theme {

/// The color of a level, shared by the records table, the level buttons and the histogram.
QColor levelColor(Level level);

/// The level's color made readable as text on the background: lighter on a dark background, darker on a
/// light one, in the same hue.
QColor levelTextColor(Level level, const QColor& background);

/// The color, lightened or darkened in its own hue until text in it has a contrast of at least 4.5:1 with
/// the background (WCAG AA). A color that has it already is returned as it is.
QColor readableOn(const QColor& color, const QColor& background);

/// The contrast ratio of two colors, from 1 (the same) to 21 (black and white), as WCAG defines it.
double contrast(const QColor& first, const QColor& second);

/// Black or white, whichever reads better on the given background.
QColor textColorOn(const QColor& background);

/// Colors of state indicators: running (the receiver, live mode), stopped or paused, failed.
QColor activeColor();
QColor inactiveColor();
QColor errorColor();

/// The fixed-width font for messages and raw lines, at the size of the application font.
QFont monospaceFont();

/// The height of a table row of one line in the font or in the monospace font: the records and the
/// statistics tables have rows of the same height.
int tableRowHeight(const QFont& font);

/// The palette with a neutral selection and alternate row color in place of the system accent color, so
/// the level colors read well on every row, selected or not, and with gray placeholder text. Works for light
/// and dark system palettes.
QPalette neutralPalette(const QPalette& system);

/// Toolbar symbols that the standard icons lack or draw too dark for a dark toolbar.
enum class Glyph { Import, Export, Start, Stop, Live };

/// An icon that draws the glyph in the application palette's colors when it is painted, so it stays visible
/// in light and dark themes. Live is gray when off and green when on (QIcon::On).
QIcon glyphIcon(Glyph glyph);

} // namespace Theme
