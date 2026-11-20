#pragma once

#include "LogStore.h"

#include <QDialog>
#include <QVector>

#include <optional>

class QComboBox;
class QDateTimeEdit;
class QRadioButton;

/// Asks which records "Delete Records Older Than" deletes (DESIGN.md sections 1.2, F3, and 6.7): those before
/// a local date and time, of every source or of one.
class DeleteRecordsDialog : public QDialog {
    Q_OBJECT

public:
    /// selectedSourceId: the source chosen at first, if one is selected in the Sources panel.
    DeleteRecordsDialog(const QVector<SourceInfo>& sources, std::optional<qint64> selectedSourceId,
                        QWidget* parent = nullptr);

    /// Records strictly before this time go, ms UTC.
    qint64 before() const;
    /// The source whose records go; 0 = every source.
    qint64 sourceId() const;
    /// The name of that source; empty for every source.
    QString sourceName() const;

private:
    QDateTimeEdit* m_before;
    QRadioButton* m_allSources;
    QRadioButton* m_oneSource;
    QComboBox* m_source;
};
