#pragma once

#include <QObject>

/// Checks the logic of the side panels: the source selection ("All" or some sources, kept across
/// refreshes) and the filters the record details turn values into.
class TestPanels : public QObject {
    Q_OBJECT

private slots:
    void sourcesStartWithAll();
    void sourcesShowOnlyOrHideOne();
    void sourcesKeepSelectionAcrossRefreshes();
    void sourcesAllAndSingleExcludeEachOther();
    void detailsShowFieldValues();
    void detailsTurnValuesIntoFilters();
};
