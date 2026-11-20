#pragma once

#include "SearchService.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QStandardItemModel;
class QTableView;

/// The Statistics tab (DESIGN.md sections 2.2 and 6.5): the matches grouped by level, source, similar
/// messages or a field, with the count and share of each group. A double-click turns a group into a filter.
/// The statistics come with the search results, and are asked for only while the tab is shown.
class StatsView : public QWidget {
    Q_OBJECT

public:
    explicit StatsView(QWidget* parent = nullptr);

    /// Asks the search for the chosen grouping when the view is shown, and for nothing otherwise or while the
    /// field name is missing or invalid. Remembers the request, so showResult() knows what the rows mean.
    void prepareRequest(SearchRequest& request, bool shown);

    /// Shows the statistics of the latest search, which prepareRequest() set up.
    void showResult(const SearchResult& result);

    void clear();

    /// The search line part for a pattern of similar messages: the longest part of its first line without
    /// '#', in quotes; empty when there is no such part.
    static QString messageSearchTerm(const QString& pattern);

    /// A group's share of the matches: "12.5 %", "< 0.1 %" for small groups.
    static QString shareText(qint64 count, qint64 total);

signals:
    /// The grouping changed, so the search should run again.
    void groupingChanged();
    /// A double-click on a group of the level, source, message or field grouping.
    void levelRequested(Level level);
    void sourceRequested(qint64 sourceId);
    void searchTermRequested(const QString& term);

protected:
    void changeEvent(QEvent* event) override;

private:
    StatsMode chosenMode() const;
    void groupingSelected();
    void showRows(const QVector<StatsRow>& rows, qint64 total);
    void updateSummary();
    void rowActivated(const QModelIndex& index);
    QString valueHeader() const;

    QComboBox* m_groupBy;
    QLineEdit* m_field;
    QLabel* m_summary;
    QTableView* m_table;
    QStandardItemModel* m_model;
    StatsMode m_requestedMode = StatsMode::None;
    QString m_requestedField;
    // What the rows in the table are.
    StatsMode m_rowsMode = StatsMode::None;
    QString m_rowsField;
    QVector<StatsRow> m_rows;
    qint64 m_total = 0;
};
