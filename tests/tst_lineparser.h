#pragma once

#include <QObject>

/// Checks LineParser: JSON lines (field names, nesting, unusable values), plain text lines, broken lines,
/// the truncated flag and level synonyms.
class TestLineParser : public QObject {
    Q_OBJECT

private slots:
    void levelSynonyms_data();
    void levelSynonyms();

    void jsonSpecialFields();
    void jsonFieldNames_data();
    void jsonFieldNames();
    void jsonFieldPriority();
    void jsonNestedObjectsAreFlattened();
    void jsonUnusableValuesStayInFields();
    void jsonNumbersAndScalars();
    void jsonWithoutMessageShowsWholeLine();

    void plainText_data();
    void plainText();

    void unrecognizedLines_data();
    void unrecognizedLines();
    void truncatedFlag();
    void rawKeepsOriginalText();
    void invalidUtf8NeverFails();
    void unicodeText();
};
