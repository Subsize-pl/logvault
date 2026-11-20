#include "tst_panels.h"

#include "RecordDetailsPanel.h"
#include "SourcesPanel.h"

#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

using namespace Qt::StringLiterals;

namespace {

const QVector<SourceInfo> kSources = {{1, u"cart"_s, 3}, {2, u"payment"_s, 5}, {3, u"web"_s, 2}};

QTreeWidget* treeOf(const QWidget& panel) { return panel.findChild<QTreeWidget*>(); }

// The details item whose first column says name.
QTreeWidgetItem* detailsItem(const RecordDetailsPanel& panel, const QString& name) {
    QTreeWidget* tree = treeOf(panel);
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == name) {
            return tree->topLevelItem(i);
        }
    }
    return nullptr;
}

Record detailedRecord() {
    Record record;
    record.ts = 1'795'168'800'000;
    record.level = Level::Error;
    record.source = u"payment"_s;
    record.message = u"Payment provider timeout\n  at Gateway.call()"_s;
    record.fields =
        uR"({"provider":"bepaid","amount":12.5,"order":8812,"retry":true,"note":null,"bad name":"x"})"_s;
    record.raw = u"raw line"_s;
    return record;
}

} // namespace

void TestPanels::sourcesStartWithAll() {
    SourcesPanel panel({});
    panel.setSources(kSources);
    QVERIFY(panel.selectedIds().isEmpty());
    QVERIFY(!panel.selectedSource());
    QTreeWidget* tree = treeOf(panel);
    QCOMPARE(tree->topLevelItemCount(), 4);
    QCOMPARE(tree->topLevelItem(0)->text(0), u"All"_s);
    QCOMPARE(tree->topLevelItem(0)->text(1), u"10"_s);
    QCOMPARE(tree->topLevelItem(2)->text(0), u"payment"_s);
    QCOMPARE(tree->topLevelItem(2)->text(1), u"5"_s);
}

void TestPanels::sourcesShowOnlyOrHideOne() {
    SourcesPanel panel({});
    panel.setSources(kSources);
    QSignalSpy changes(&panel, &SourcesPanel::selectionChanged);

    panel.showOnlySource(u"payment"_s);
    QCOMPARE(panel.selectedIds(), QList<qint64>{2});
    QCOMPARE(panel.selectedSource()->name, u"payment"_s);
    QCOMPARE(changes.count(), 1);

    // Hiding the only source shown leaves all the others.
    panel.hideSource(u"payment"_s);
    QCOMPARE(panel.selectedIds(), (QList<qint64>{1, 3}));
    panel.hideSource(u"web"_s);
    QCOMPARE(panel.selectedIds(), QList<qint64>{1});
    QCOMPARE(changes.count(), 3);

    panel.hideSource(u"unknown"_s);
    QCOMPARE(changes.count(), 3);
}

void TestPanels::sourcesKeepSelectionAcrossRefreshes() {
    SourcesPanel panel({});
    panel.setSources(kSources);
    panel.showOnlySource(u"web"_s);
    QSignalSpy changes(&panel, &SourcesPanel::selectionChanged);

    // New counts and a new source keep the selection.
    QVector<SourceInfo> refreshed = kSources;
    refreshed[2].recordCount = 40;
    refreshed.append({4, u"zeta"_s, 1});
    panel.setSources(refreshed);
    QCOMPARE(panel.selectedIds(), QList<qint64>{3});
    QCOMPARE(treeOf(panel)->topLevelItem(3)->text(1), u"40"_s);
    QCOMPARE(changes.count(), 0);

    // The selected source is gone (deleted): back to "All".
    refreshed.removeAt(2);
    panel.setSources(refreshed);
    QVERIFY(panel.selectedIds().isEmpty());
    QCOMPARE(changes.count(), 1);
}

void TestPanels::sourcesAllAndSingleExcludeEachOther() {
    SourcesPanel panel({});
    panel.setSources(kSources);
    QTreeWidget* tree = treeOf(panel);

    // Adding sources to "All" (Ctrl+click) drops "All"...
    tree->topLevelItem(1)->setSelected(true);
    tree->topLevelItem(3)->setSelected(true);
    QVERIFY(!tree->topLevelItem(0)->isSelected());
    QCOMPARE(panel.selectedIds(), (QList<qint64>{1, 3}));

    // ...adding "All" to sources drops the sources, and an empty selection becomes "All".
    tree->topLevelItem(0)->setSelected(true);
    QVERIFY(panel.selectedIds().isEmpty());
    QCOMPARE(tree->selectedItems().size(), 1);
    tree->clearSelection();
    QVERIFY(tree->topLevelItem(0)->isSelected());
}

void TestPanels::detailsShowFieldValues() {
    RecordDetailsPanel panel;
    panel.showRecord(detailedRecord());
    QCOMPARE(detailsItem(panel, u"Level"_s)->text(1), u"ERROR"_s);
    QCOMPARE(detailsItem(panel, u"Source"_s)->text(1), u"payment"_s);
    // The tree shows one line; the raw line below shows everything.
    QCOMPARE(detailsItem(panel, u"Message"_s)->text(1), u"Payment provider timeout"_s);
    QCOMPARE(detailsItem(panel, u"amount"_s)->text(1), u"12.5"_s);
    QCOMPARE(detailsItem(panel, u"order"_s)->text(1), u"8812"_s);
    QCOMPARE(detailsItem(panel, u"retry"_s)->text(1), u"true"_s);
    QCOMPARE(detailsItem(panel, u"note"_s)->text(1), u"null"_s);

    panel.clear();
    QCOMPARE(treeOf(panel)->topLevelItemCount(), 0);
}

void TestPanels::detailsTurnValuesIntoFilters() {
    RecordDetailsPanel panel;
    panel.showRecord(detailedRecord());
    QSignalSpy terms(&panel, &RecordDetailsPanel::searchTermRequested);
    QSignalSpy levels(&panel, &RecordDetailsPanel::levelFilterRequested);
    QSignalSpy sources(&panel, &RecordDetailsPanel::sourceFilterRequested);
    QTreeWidget* tree = treeOf(panel);
    const auto doubleClick = [&](const QString& name) {
        emit tree->itemDoubleClicked(detailsItem(panel, name), 1);
    };

    doubleClick(u"provider"_s);
    doubleClick(u"order"_s);
    doubleClick(u"Message"_s);
    QCOMPARE(terms.count(), 3);
    QCOMPARE(terms[0].first().toString(), u"provider:\"bepaid\""_s);
    QCOMPARE(terms[1].first().toString(), u"order:\"8812\""_s);
    QCOMPARE(terms[2].first().toString(), u"\"Payment provider timeout\""_s);

    doubleClick(u"Level"_s);
    QCOMPARE(levels.count(), 1);
    QCOMPARE(levels.first().first().value<Level>(), Level::Error);
    QCOMPARE(levels.first().last().toBool(), false);
    doubleClick(u"Source"_s);
    QCOMPARE(sources.count(), 1);
    QCOMPARE(sources.first().first().toString(), u"payment"_s);

    // A time, a null and a name the search line cannot hold have nothing to search for.
    doubleClick(u"Time"_s);
    doubleClick(u"note"_s);
    doubleClick(u"bad name"_s);
    QCOMPARE(terms.count(), 3);
    QCOMPARE(levels.count() + sources.count(), 2);
}
