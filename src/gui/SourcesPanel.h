#pragma once

#include "LogStore.h"

#include <QList>
#include <QVector>
#include <QWidget>

#include <optional>

class QAction;
class QTreeWidget;
class QTreeWidgetItem;

/// The "Sources" panel (DESIGN.md section 2.2): "All" and every source with its record count. The selected
/// sources filter the search, several at a time.
class SourcesPanel : public QWidget {
    Q_OBJECT

public:
    /// contextActions (Rename Source, Delete Source) make up the context menu; they act on selectedSource().
    explicit SourcesPanel(const QList<QAction*>& contextActions, QWidget* parent = nullptr);

    /// Shows new sources and counts. Selected sources that are still there stay selected; when none is
    /// left, "All" is selected and selectionChanged() is emitted.
    void setSources(const QVector<SourceInfo>& sources);

    QVector<SourceInfo> sources() const;

    /// The ids of the selected sources, sorted; empty when "All" is selected.
    QList<qint64> selectedIds() const;

    /// The selected source when exactly one source (not "All") is selected.
    std::optional<SourceInfo> selectedSource() const;

    /// Selects only the named source, or takes it out of the sources shown. Emits selectionChanged().
    void showOnlySource(const QString& name);
    void hideSource(const QString& name);

signals:
    /// The selected sources changed, so the search should run again.
    void selectionChanged();

private:
    void rebuild(const QList<qint64>& selected);
    void updateCounts();
    void selectIds(const QList<qint64>& ids);
    void normalizeSelection();
    void notifyIfChanged();

    QTreeWidget* m_tree;
    QTreeWidgetItem* m_all = nullptr;
    QVector<SourceInfo> m_sources; // in the order of the items after "All"
    QList<qint64> m_notifiedIds;
    bool m_allWasSelected = true;
};
