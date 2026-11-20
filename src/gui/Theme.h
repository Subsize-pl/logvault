#pragma once

#include "Record.h"

#include <QColor>
#include <QFont>

/// Colors and fonts shared by the widgets (DESIGN.md section 2.4).
namespace Theme {

/// The color of a level, shared by the records table, the level buttons and the histogram.
QColor levelColor(Level level);

/// Black or white, whichever reads better on the given background.
QColor textColorOn(const QColor& background);

/// Colors of state indicators: running (the receiver, live mode), stopped or paused, failed.
QColor activeColor();
QColor inactiveColor();
QColor errorColor();

/// The fixed-width font for messages and raw lines, at the size of the application font.
QFont monospaceFont();

} // namespace Theme
