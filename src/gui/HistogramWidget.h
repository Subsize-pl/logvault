#pragma once

#include "SearchQuery.h"

#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <utility>

class QPainter;

/// The histogram of record counts over time (DESIGN.md sections 2.2 and 6.4): one bar per time step, stacked
/// by level in the level colors, with a time axis and a tooltip per bar. Dragging across bars selects a
/// period to zoom into, and Esc asks for the period before. It paints the histogram of the last search result
/// only and never reads the database.
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    /// Shows the histogram of a search result.
    void setHistogram(const Histogram& histogram);
    /// Shows nothing, as while no database is open.
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// The period covered by the bars from the one at time first to the one at time last (in any order), ms
    /// UTC with the end exclusive. Times outside the bars count as the nearest bar; {0, 0} without bars.
    static std::pair<qint64, qint64> selectionPeriod(const Histogram& histogram, qint64 first, qint64 last);

    /// The time between two axis labels: the first round interval (1 s, 5 s, 10 s, ... 1 h, 3 h, ... 1 day,
    /// 7 days, ...) that is a multiple of the bar step and puts at least minSpacing pixels between labels
    /// when a bar is barWidth pixels wide.
    static qint64 labelInterval(qint64 step, qreal barWidth, qreal minSpacing);

    /// The bar boundaries (0 = the start of the first bar) that get a label: those at local times that are a
    /// multiple of the interval, or at such UTC times when there are none (days start at UTC midnight).
    static QVector<int> labelBoundaries(const Histogram& histogram, qint64 interval);

    /// A label as precise as the interval needs; a label at local midnight shows the date instead.
    static QString labelText(qint64 msecs, qint64 interval);

    /// The tooltip of a bar (rich text): its interval, its records and the count of each level in it.
    static QString toolTipText(const Histogram& histogram, int bar);

signals:
    /// The user dragged across bars: the period they cover, ms UTC, the end exclusive.
    void periodSelected(qint64 from, qint64 to);
    /// Esc: go back to the period before the last zoom.
    void previousPeriodRequested();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotArea() const;
    QRectF barColumn(const QRectF& plot, int bar) const;
    int barAt(qreal x) const;
    qint64 timeAt(qreal x) const;
    void drawBars(QPainter& painter, const QRectF& plot) const;
    void drawSelection(QPainter& painter, const QRectF& plot) const;
    void drawAxes(QPainter& painter, const QRectF& plot) const;
    void drawMessage(QPainter& painter, const QRectF& area, const QString& text) const;
    void showToolTip(const QPoint& globalPosition, int bar);
    QColor axisColor(int alpha) const;

    Histogram m_histogram;
    qint64 m_maxTotal = 0; // records in the fullest bar
    bool m_hasResult = false;
    int m_hoveredBar = -1;
    // A drag remembers times, not bars: in live mode a new histogram can arrive in the middle of it.
    bool m_dragging = false;
    qreal m_pressX = 0;
    qint64 m_anchorTime = 0;
    qint64 m_currentTime = 0;
};
