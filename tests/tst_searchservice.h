#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

/// Checks SearchService: results arrive asynchronously, results of stale searches are dropped, errors come
/// back as results, and searching works while another connection writes. Any Qt warning fails a test.
class TestSearchService : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void deliversResult();
    void computesStatisticsOnlyWhenAsked();
    void dropsStaleResults();
    void dropsResultsOfPreviousDatabase();
    void reportsErrors();
    void keepsBarsOfEmptyBoundedPeriod();
    void searchesWhileRecordsArrive();
    void destroysWhileSearching();

private:
    QString databasePath(const QString& fileName = QStringLiteral("service.lvdb")) const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
