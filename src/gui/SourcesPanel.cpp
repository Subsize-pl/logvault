#include "SourcesPanel.h"

#include "Format.h"

#include <QHeaderView>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Column { NameColumn, CountColumn, ColumnCount };

constexpr int kIdRole = Qt::UserRole;

bool sameSources(const QVector<SourceInfo>& a, const QVector<SourceInfo>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const SourceInfo& x, const SourceInfo& y) {
        return x.id == y.id && x.name == y.name;
    });
}

} // namespace

SourcesPanel::SourcesPanel(const QList<QAction*>& contextActions, QWidget* parent)
    : QWidget(parent), m_tree(new QTreeWidget) {
    m_tree->setColumnCount(ColumnCount);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(CountColumn, QHeaderView::ResizeToContents);
    m_tree->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_tree->addActions(contextActions);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SourcesPanel::normalizeSelection);
    rebuild({});
}

void SourcesPanel::setSources(const QVector<SourceInfo>& sources) {
    const bool sameItems = sameSources(sources, m_sources);
    m_sources = sources;
    // Live mode refreshes the counts every second; rebuilding the items only when sources come, go or are
    // renamed keeps the list from flickering.
    if (sameItems) {
        updateCounts();
    } else {
        rebuild(selectedIds());
    }
    notifyIfChanged();
}

QVector<SourceInfo> SourcesPanel::sources() const { return m_sources; }

QList<qint64> SourcesPanel::selectedIds() const {
    QList<qint64> ids;
    if (m_all->isSelected()) {
        return ids;
    }
    for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
        ids.append(item->data(NameColumn, kIdRole).toLongLong());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::optional<SourceInfo> SourcesPanel::selectedSource() const {
    const QList<qint64> ids = selectedIds();
    if (ids.size() == 1) {
        for (const SourceInfo& source : m_sources) {
            if (source.id == ids.front()) {
                return source;
            }
        }
    }
    return std::nullopt;
}

void SourcesPanel::showOnlySource(const QString& name) {
    for (const SourceInfo& source : m_sources) {
        if (source.name == name) {
            showOnlySourceId(source.id);
            return;
        }
    }
}

void SourcesPanel::showOnlySourceId(qint64 id) {
    const bool listed = std::any_of(m_sources.begin(), m_sources.end(),
                                    [id](const SourceInfo& source) { return source.id == id; });
    if (listed) {
        selectIds({id});
        notifyIfChanged();
    }
}

void SourcesPanel::hideSource(const QString& name) {
    const auto hidden = std::find_if(m_sources.begin(), m_sources.end(),
                                     [&name](const SourceInfo& source) { return source.name == name; });
    if (hidden == m_sources.end()) {
        return;
    }
    QList<qint64> shown = selectedIds();
    shown.removeAll(hidden->id);
    if (shown.isEmpty()) {
        // "All" was selected, or only this source: show every other one.
        for (const SourceInfo& source : m_sources) {
            if (source.id != hidden->id) {
                shown.append(source.id);
            }
        }
    }
    // With no other source in the database there is nothing to show instead, so the selection stays.
    if (!shown.isEmpty()) {
        selectIds(shown);
        notifyIfChanged();
    }
}

void SourcesPanel::rebuild(const QList<qint64>& selected) {
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
        m_all = new QTreeWidgetItem(m_tree, {tr("All"), QString()});
        m_all->setData(NameColumn, kIdRole, qint64(0));
        m_all->setTextAlignment(CountColumn, Qt::AlignRight | Qt::AlignVCenter);
        for (const SourceInfo& source : m_sources) {
            auto* item = new QTreeWidgetItem(m_tree, {source.name, QString()});
            item->setData(NameColumn, kIdRole, source.id);
            item->setTextAlignment(CountColumn, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    updateCounts();
    selectIds(selected);
}

void SourcesPanel::updateCounts() {
    qint64 total = 0;
    for (int i = 0; i < m_sources.size(); ++i) {
        m_tree->topLevelItem(i + 1)->setText(CountColumn, Format::count(m_sources[i].recordCount));
        total += m_sources[i].recordCount;
    }
    m_all->setText(CountColumn, Format::count(total));
}

void SourcesPanel::selectIds(const QList<qint64>& ids) {
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
        bool anySelected = false;
        for (int i = 1; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_tree->topLevelItem(i);
            if (ids.contains(item->data(NameColumn, kIdRole).toLongLong())) {
                item->setSelected(true);
                anySelected = true;
            }
        }
        if (!anySelected) {
            m_all->setSelected(true);
        }
    }
    m_allWasSelected = m_all->isSelected();
}

void SourcesPanel::normalizeSelection() {
    {
        const QSignalBlocker blocker(m_tree);
        const qsizetype selectedCount = m_tree->selectedItems().size();
        if (selectedCount == 0) {
            // No selection means every source, so show it as "All".
            m_all->setSelected(true);
        } else if (m_all->isSelected() && selectedCount > 1) {
            // "All" and single sources exclude each other: whichever was picked last wins.
            if (m_allWasSelected) {
                m_all->setSelected(false);
            } else {
                m_tree->clearSelection();
                m_all->setSelected(true);
            }
        }
    }
    m_allWasSelected = m_all->isSelected();
    notifyIfChanged();
}

void SourcesPanel::notifyIfChanged() {
    const QList<qint64> ids = selectedIds();
    if (ids != m_notifiedIds) {
        m_notifiedIds = ids;
        emit selectionChanged();
    }
}
