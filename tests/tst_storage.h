#pragma once

#include <QObject>

/// Checks the record helpers and the storage layer on temporary database files.
class TestStorage : public QObject {
    Q_OBJECT

private slots:
    void levelNamesAndValues();
    void messageKeyGroupsDigits_data();
    void messageKeyGroupsDigits();
    void messageKeyKeepsSurrogatePairsWhole();
};
