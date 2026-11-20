#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

/// Checks DatabaseJobs on temporary databases: opening and creating files, renaming and deleting sources,
/// cancelling a deletion and refusing a second job. Any Qt warning fails a test.
class TestDatabaseJobs : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensAndCreatesDatabase();
    void reportsFileThatIsNotDatabase();
    void renamesSource();
    void reportsTakenName();
    void deletesSourceWithItsRecords();
    void cancelKeepsSourceAndRemainingRecords();
    void runsOneJobAtATime();

private:
    QString databasePath() const;
    /// Writes count records of each named source into the database.
    bool fill(const QStringList& sources, int count) const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
