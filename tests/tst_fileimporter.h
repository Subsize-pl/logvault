#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

/// Checks FileImporter on generated files: line breaks, BOM, stack traces, limits, errors, cancellation,
/// progress and the speed on 100,000 lines. Any Qt warning fails a test.
class TestFileImporter : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void importsCrlfAndBom();
    void joinsIndentedLines();
    void importsEmptyFiles();
    void countsUnparsedLines();
    void namesSourcesPerFile();
    void limitsLongLinesAndRecords();
    void reportsMissingFileAndKeepsEarlierLines();
    void rejectsUtf16File();
    void reportsDatabaseFailure();
    void refusesSecondStartWhileRunning();
    void cancelKeepsCommittedTransactions();
    void importsHundredThousandLines();

private:
    QString databasePath() const;
    QString writeFile(const QString& name, const QByteArray& content) const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
