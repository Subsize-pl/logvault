#include "RecordTableModel.h"

#include "Format.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QPalette>
#include <QStringList>

#include <algorithm>
#include <utility>

RecordTableModel::RecordTableModel(QObject* parent)
    : QAbstractTableModel(parent), m_messageFont(Theme::monospaceFont()) {
    m_levelFont.setBold(true);
}

void RecordTableModel::setRecords(QVector<Record> records) {
    beginResetModel();
    m_records = std::move(records);
    endResetModel();
}

const Record& RecordTableModel::record(int row) const { return m_records.at(row); }

QString RecordTableModel::rawLines(QList<int> rows) const {
    std::sort(rows.begin(), rows.end());
    QStringList lines;
    lines.reserve(rows.size());
    for (const int row : rows) {
        if (row >= 0 && row < m_records.size()) {
            lines.append(m_records[row].raw);
        }
    }
    return lines.join(u'\n');
}

int RecordTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_records.size());
}

int RecordTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RecordTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_records.size()) {
        return {};
    }
    const Record& record = m_records[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return displayText(record, index.column());
    case Qt::ForegroundRole:
        if (index.column() == LevelColumn) {
            // Selected rows use the palette's highlighted text instead, which reads on the selection color.
            return Theme::levelTextColor(record.level, QGuiApplication::palette().color(QPalette::Base));
        }
        break;
    case Qt::FontRole:
        if (index.column() == LevelColumn) {
            return m_levelFont;
        }
        if (index.column() == MessageColumn) {
            return m_messageFont;
        }
        break;
    default:
        break;
    }
    return {};
}

QVariant RecordTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
    case TimeColumn:
        return tr("Time");
    case LevelColumn:
        return tr("Level");
    case SourceColumn:
        return tr("Source");
    case MessageColumn:
        return tr("Message");
    default:
        return {};
    }
}

QString RecordTableModel::displayText(const Record& record, int column) const {
    switch (column) {
    case TimeColumn:
        return Format::timestamp(record.ts);
    case LevelColumn:
        return levelName(record.level);
    case SourceColumn:
        return record.source;
    case MessageColumn:
        return Format::firstLine(record.message);
    default:
        return {};
    }
}
