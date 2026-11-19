#pragma once

#include <QObject>

/// Checks LineSplitter: line breaks, lines split across reads, blank lines and the 64 KB limit.
class TestLineSplitter : public QObject {
    Q_OBJECT

private slots:
    void splitsLinesAndDropsCarriageReturns();
    void keepsUnfinishedLineAcrossCalls();
    void sameLinesWhenFedByteByByte();
    void skipsBlankLines();
    void finishReturnsTheTail();
    void cutsLongLines_data();
    void cutsLongLines();
    void limitIsExact();
    void cutNeverSplitsUtf8Characters();
    void skipsRestOfLongLineUntilNewline();
};
