#pragma once

#include "Record.h"

#include <QTimeZone>
#include <QWidget>

class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

/// The "Record details" panel (DESIGN.md section 2.2): every field of the selected record and its raw line.
/// A value can be copied or turned into a filter: a part of the search line, or for the level and the
/// source, the level buttons and the source selection.
class RecordDetailsPanel : public QWidget {
    Q_OBJECT

public:
    explicit RecordDetailsPanel(QWidget* parent = nullptr);

    void showRecord(const Record& record);
    void clear();

    /// The zone of the record's time; local time at first.
    void setTimeZone(const QTimeZone& zone);

signals:
    /// "Search for this value" or "Exclude this value" on the message or a field: a part for the search line.
    void searchTermRequested(const QString& term);
    /// The same for the level and the source.
    void levelFilterRequested(Level level, bool exclude);
    void sourceFilterRequested(const QString& source, bool exclude);

protected:
    void changeEvent(QEvent* event) override;

private:
    enum class Kind { Time, Level, Source, Message, Field };

    void showAgain();
    void addItem(Kind kind, const QString& name, const QString& value, bool filterable);
    void showContextMenu(const QPoint& position);
    void filterBy(const QTreeWidgetItem* item, bool exclude);

    QTreeWidget* m_fields;
    QPlainTextEdit* m_raw;
    Record m_record; // the record shown
    QTimeZone m_zone{QTimeZone::LocalTime};
};
