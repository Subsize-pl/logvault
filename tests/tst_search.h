#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

/// Checks the search queries of LogStore on a database with known records: order and limits, the search
/// line and the filters on real data, the histogram and its step, statistics, the read snapshot, and (with
/// LV_PERF=1) the speed on 1,000,000 records.
class TestSearch : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void ordersNewestFirst();
    void limitsRows();
    void findsTheSpanOfMatches();
    void searchLineMatchesRecords_data();
    void searchLineMatchesRecords();
    void injectionAttemptsChangeNothing();
    void filtersMatchRecords_data();
    void filtersMatchRecords();
    void hugeSearchLineDoesNotCrash();

    void histogramCountsEveryMatch();
    void histogramStep_data();
    void histogramStep();
    void histogramStepFitsAnyRange();

    void statisticsByLevel();
    void statisticsBySource();
    void statisticsByMessage();
    void statisticsByField_data();
    void statisticsByField();
    void statisticsFollowTheCondition();
    void statisticsAreLimited();
    void statisticsRejectBadFieldName();
    void fieldMentionIsOnlyAHint();

    void readTransactionKeepsOneState();

    void searchesMillionRecordsQuickly();

private:
    QString databasePath() const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
