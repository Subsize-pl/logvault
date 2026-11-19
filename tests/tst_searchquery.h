#pragma once

#include <QObject>

/// Checks how SearchQuery turns filters and the search line into SQL with bound values (DESIGN.md
/// section 6.2): every example, quotes, exclusions, escaping and injection attempts.
class TestSearchQuery : public QObject {
    Q_OBJECT

private slots:
    void searchLine_data();
    void searchLine();
    void injectionStaysInValues_data();
    void injectionStaysInValues();

    void levelFilter_data();
    void levelFilter();
    void sourceFilter();
    void periodFilter();
    void filtersAndSearchLineCombine();

    void validatesFieldNames_data();
    void validatesFieldNames();
    void quoteRoundTrips_data();
    void quoteRoundTrips();
};
