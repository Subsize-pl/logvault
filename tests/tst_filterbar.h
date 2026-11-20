#pragma once

#include <QObject>

/// Checks how the filter bar fills a search request: level buttons (Ctrl+click), periods, the search line
/// and its history.
class TestFilterBar : public QObject {
    Q_OBJECT

private slots:
    void startsWithEverything();
    void togglesLevels();
    void ctrlClickKeepsOnlyOneLevel();
    void relativePeriodEndsNow();
    void customPeriodIsKept();
    void addsSearchTermsAndRemembersThem();
    void keepsTwentySearchesWithoutRepeats();
};
