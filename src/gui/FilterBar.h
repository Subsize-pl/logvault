#pragma once

#include "SearchQuery.h"

#include <QString>
#include <QTimeZone>
#include <QVector>
#include <QWidget>

#include <array>
#include <utility>

class QComboBox;
class QLineEdit;
class QStringListModel;
class QToolButton;

/// The filters above the records (DESIGN.md section 2.2): a toggle button per level in its color, the
/// period with presets and a custom range, and the search line with the recent searches.
class FilterBar : public QWidget {
    Q_OBJECT

public:
    static constexpr int kHistorySize = 20;
    /// Zooms that Esc can step back through.
    static constexpr int kZoomHistorySize = 50;

    explicit FilterBar(QWidget* parent = nullptr);

    /// Puts the enabled levels, the period and the search line into a request. Relative periods ("Last
    /// hour") end at now, ms UTC, so they move forward with every search.
    void fillRequest(SearchRequest& request, qint64 now) const;

    /// Keeps only this level on, or turns it off; emits searchRequested().
    void showOnlyLevel(Level level);
    void hideLevel(Level level);

    /// Switches to a custom period (ms UTC, the end exclusive), remembering the period in effect, and emits
    /// searchRequested(). The histogram zooms in with it.
    void zoomToPeriod(qint64 from, qint64 to);

    /// Goes back to the period in effect before the last zoom and emits searchRequested(). Returns false when
    /// there is none: no zoom since the user last chose a period.
    bool restorePreviousPeriod();

    /// Appends a part to the search line and searches.
    void addSearchTerm(const QString& term);

    void focusSearchLine();

    /// The zone a custom period is shown and entered in; local time at first.
    void setTimeZone(const QTimeZone& zone);

signals:
    /// A filter changed or a search was asked for: the search should run again.
    void searchRequested();

private:
    enum class Period { Last15Minutes, LastHour, Last24Hours, Last7Days, AllTime, Custom };

    // A period as the combo box holds it, so it can be put back.
    struct PeriodState {
        int index = 0;
        qint64 customFrom = 0;
        qint64 customTo = 0;
    };

    QWidget* createLevelButtons();
    void createPeriodBox();
    void createSearchLine();
    void levelClicked(Level level);
    void periodActivated(int index);
    bool chooseCustomPeriod();
    void applyCustomPeriod(qint64 from, qint64 to);
    void showCustomPeriod();
    /// from and to in ms UTC, 0 = unbounded.
    std::pair<qint64, qint64> range(Period period, qint64 now) const;
    Period currentPeriod() const;
    PeriodState periodState() const;
    void submitSearch();
    void remember(const QString& text);

    std::array<QToolButton*, kLevelCount> m_levelButtons{}; // indexed by the Level value
    QComboBox* m_period = nullptr;
    int m_periodIndex = 0; // the period in effect; the combo goes back to it when the dialog is cancelled
    qint64 m_customFrom = 0;
    qint64 m_customTo = 0;
    QVector<PeriodState> m_zoomHistory; // the periods before each zoom, the latest last
    QTimeZone m_zone{QTimeZone::LocalTime};
    QLineEdit* m_searchLine = nullptr;
    QStringListModel* m_history = nullptr;
    QString m_submittedText;
};
