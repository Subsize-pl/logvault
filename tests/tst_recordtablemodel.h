#pragma once

#include <QObject>

/// Checks what the records table shows: texts, colors, fonts, the first line of long messages and the raw
/// lines that Ctrl+C copies. QAbstractItemModelTester checks the model's consistency on the way.
class TestRecordTableModel : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty();
    void showsHeaders();
    void showsRecordTexts();
    void showsTimesInTheChosenZone();
    void showsFirstLineOfMessage();
    void cutsLongLinesBetweenCharacters();
    void colorsLevelsAndUsesFonts();
    void copiesRawLinesInRowOrder();
    void replacesRowsWithReset();
};
