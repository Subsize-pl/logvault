#pragma once

#include <QObject>

/// Checks the settings: defaults, a round trip through a settings file, damaged values read as defaults,
/// the values derived from them, the settings dialog, and the list of recent databases.
class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void emptyFileGivesDefaults();
    void savedSettingsReadBack();
    void damagedValuesReadAsDefaults();
    void derivedValuesFollowTheChoices();
    void dialogEditsACopyOfTheSettings();
    void recentListKeepsTheNewestFirst();
    void recentListRemovesOnlyThatFile();
    void recentListIsCleanedUpWhenRead();
};
