#pragma once

#include <QObject>

/// Checks the histogram: a selection becomes a period of whole bars, the axis labels sit on round times at a
/// spacing that fits the bars, the tooltip, and the mouse and Esc handling of the widget.
class TestHistogram : public QObject {
    Q_OBJECT

private slots:
    void selectionCoversWholeBars();
    void labelIntervalFitsTheBars_data();
    void labelIntervalFitsTheBars();
    void labelsSitOnRoundTimes();
    void labelTextMatchesTheInterval();
    void toolTipShowsIntervalAndLevels();
    void dragSelectsAPeriod();
    void clickAndEscapeSelectNothing();
};
