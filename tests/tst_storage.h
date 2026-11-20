#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

/// Checks the record helpers and the storage layer on temporary database files.
class TestStorage : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void levelNamesAndValues();
    void messageKeyGroupsDigits_data();
    void messageKeyGroupsDigits();
    void messageKeyKeepsSurrogatePairsWhole();

    void createsSchemaAndReopens();
    void rejectsNewerSchemaVersion();
    void rejectsForeignSqliteFile();
    void rejectsNonSqliteFile();
    void rejectsMissingDirectory();
    void opensPathWithSpacesAndNonAscii();

    void reopenKeepsRecords();
    void insertsHundredThousandRecordsQuickly();
    void createsSourcesWithCounts();
    void storesEmptyTextsAndSkipsEmptyBatch();
    void failedBatchWritesNothing();
    void renamesSource();
    void rejectsBadRenames();
    void deleteSourceRemovesItsRecords();
    void deleteSourceCanBeCancelled();
    void deleteOlderThanIsStrict();
    void deletionReportsProgress();
    void unicodeRoundTrip();
    void writersAndReaderInParallel();

private:
    QString databasePath(const QString& fileName = QStringLiteral("test.lvdb")) const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
