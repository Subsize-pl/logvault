#pragma once

#include <QObject>

/// Checks the SQLite features LogVault relies on: the QSQLITE driver, its JSON functions and WAL mode.
class TestEnvironment : public QObject {
    Q_OBJECT

private slots:
    void sqliteDriverIsAvailable();
    void jsonExtractReadsNestedField();
    void walModeCanBeEnabled();
};
