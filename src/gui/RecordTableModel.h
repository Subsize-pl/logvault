#pragma once

#include "Record.h"

#include <QAbstractTableModel>
#include <QFont>
#include <QList>
#include <QVector>

/// The records of the last search as the records table shows them (DESIGN.md section 2.2): time, level in
/// its color, source, and the first line of the message in a fixed-width font. Rows keep the order of the
/// search result, newest first.
class RecordTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { TimeColumn, LevelColumn, SourceColumn, MessageColumn, ColumnCount };

    explicit RecordTableModel(QObject* parent = nullptr);

    /// Replaces every row. Views lose their selection without a selectionChanged() signal.
    void setRecords(QVector<Record> records);

    /// The record in a row; the row must exist.
    const Record& record(int row) const;

    /// The raw lines of the given rows, top to bottom, separated by line breaks (what Ctrl+C copies).
    QString rawLines(QList<int> rows) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QString displayText(const Record& record, int column) const;

    QVector<Record> m_records;
    QFont m_levelFont;
    QFont m_messageFont;
};
