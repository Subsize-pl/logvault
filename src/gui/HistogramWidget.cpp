#include "HistogramWidget.h"

#include "Format.h"
#include "Theme.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTime>
#include <QToolTip>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kSecond = 1000;
constexpr qint64 kMinute = 60 * kSecond;
constexpr qint64 kHour = 60 * kMinute;
constexpr qint64 kDay = 24 * kHour;

// Round intervals between time labels, shortest first: the bar steps and the steps between them, counted in
// weeks and in 30 days as the long bar steps are.
constexpr std::array<qint64, 30> kLabelIntervals = {
    kSecond,      5 * kSecond,  10 * kSecond, 15 * kSecond, 30 * kSecond, kMinute,   2 * kMinute, 5 * kMinute,
    10 * kMinute, 15 * kMinute, 30 * kMinute, kHour,        2 * kHour,    3 * kHour, 6 * kHour,   12 * kHour,
    kDay,         2 * kDay,     7 * kDay,     14 * kDay,    28 * kDay,    30 * kDay, 56 * kDay,   60 * kDay,
    90 * kDay,    91 * kDay,    180 * kDay,   182 * kDay,   360 * kDay,   364 * kDay};

// Bottom to top, and the order of the tooltip: the most severe levels stand on the axis, where the bars are
// easy to compare, so a spike of errors shows at once.
constexpr std::array kStackOrder = {Level::Fatal, Level::Error, Level::Warn,   Level::Info,
                                    Level::Debug, Level::Trace, Level::Unknown};

constexpr int kPadding = 6;
constexpr int kTickLength = 3;
// Narrower bars are drawn without a gap, which would leave no bar at all.
constexpr qreal kMinGappedBarWidth = 4;
constexpr int kHoverAlpha = 45;
constexpr int kSelectionAlpha = 110;

constexpr QChar kEnDash(0x2013);
constexpr QChar kSquare(0x25A0);

qint64 totalOf(const LevelCounts& counts) { return std::accumulate(counts.begin(), counts.end(), qint64(0)); }

qint64 countOf(const LevelCounts& counts, Level level) { return counts[static_cast<std::size_t>(level)]; }

// The remainder that is never negative, also for times before 1970.
qint64 floorMod(qint64 value, qint64 divisor) {
    const qint64 remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

// How far the zone is ahead of UTC at a moment, in ms.
qint64 offsetAt(qint64 msecs, const QTimeZone& zone) {
    return QDateTime::fromMSecsSinceEpoch(msecs, zone).offsetFromUtc() * kSecond;
}

} // namespace

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    // A click gives the histogram the focus, so Esc after a zoom comes here.
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void HistogramWidget::setHistogram(const Histogram& histogram) {
    m_histogram = histogram;
    m_hasResult = true;
    m_maxTotal = 0;
    for (const LevelCounts& bar : m_histogram.bars) {
        m_maxTotal = qMax(m_maxTotal, totalOf(bar));
    }
    if (m_histogram.bars.isEmpty()) {
        m_dragging = false;
        unsetCursor();
    } else {
        setCursor(Qt::CrossCursor);
    }
    // Live mode brings a new histogram every second; the bar under the mouse and its tooltip follow it.
    m_hoveredBar = underMouse() ? barAt(mapFromGlobal(QCursor::pos()).x()) : -1;
    if (m_hoveredBar >= 0 && !m_dragging && QToolTip::isVisible()) {
        showToolTip(QCursor::pos(), m_hoveredBar);
    }
    update();
}

void HistogramWidget::clear() {
    m_histogram = {};
    m_maxTotal = 0;
    m_hasResult = false;
    m_hoveredBar = -1;
    m_dragging = false;
    unsetCursor();
    update();
}

void HistogramWidget::setTimeZone(const QTimeZone& zone) {
    m_zone = zone;
    update();
}

QSize HistogramWidget::sizeHint() const { return {400, 120}; }

QSize HistogramWidget::minimumSizeHint() const { return {160, 80}; }

std::pair<qint64, qint64> HistogramWidget::selectionPeriod(const Histogram& histogram, qint64 first,
                                                           qint64 last) {
    const qsizetype count = histogram.bars.size();
    if (count == 0 || histogram.step <= 0) {
        return {0, 0};
    }
    const auto barOf = [&histogram, count](qint64 time) {
        // Division rounds towards zero, and the clamp takes care of times before the first bar.
        return std::clamp<qint64>((time - histogram.start) / histogram.step, 0, count - 1);
    };
    const qint64 firstBar = barOf(qMin(first, last));
    const qint64 lastBar = barOf(qMax(first, last));
    return {histogram.start + firstBar * histogram.step, histogram.start + (lastBar + 1) * histogram.step};
}

qint64 HistogramWidget::labelInterval(qint64 step, qreal barWidth, qreal minSpacing) {
    if (step <= 0 || barWidth <= 0) {
        return step;
    }
    const auto fits = [step, barWidth, minSpacing](qint64 interval) {
        return interval % step == 0 && static_cast<qreal>(interval / step) * barWidth >= minSpacing;
    };
    const auto round = std::find_if(kLabelIntervals.begin(), kLabelIntervals.end(), fits);
    if (round != kLabelIntervals.end()) {
        return *round;
    }
    // Very long ranges have bars of many 30 days: then any multiple of the step that leaves the room.
    return step * qMax<qint64>(1, static_cast<qint64>(std::ceil(minSpacing / barWidth)));
}

QVector<int> HistogramWidget::labelBoundaries(const Histogram& histogram, qint64 interval,
                                              const QTimeZone& zone) {
    QVector<int> boundaries;
    if (histogram.bars.isEmpty() || histogram.step <= 0 || interval <= 0) {
        return boundaries;
    }
    const auto collect = [&histogram, interval, &zone, &boundaries](bool inZone) {
        for (int boundary = 0; boundary <= histogram.bars.size(); ++boundary) {
            const qint64 time = histogram.start + boundary * histogram.step;
            if (floorMod(inZone ? time + offsetAt(time, zone) : time, interval) == 0) {
                boundaries.append(boundary);
            }
        }
    };
    // Labels at round times of the zone read best (14:00, midnight). Bars of whole days start at UTC
    // midnight, which is no round local time in most time zones; those are labeled at round UTC times.
    collect(true);
    if (boundaries.isEmpty()) {
        collect(false);
    }
    return boundaries;
}

QString HistogramWidget::labelText(qint64 msecs, qint64 interval, const QTimeZone& zone) {
    const QDateTime time = QDateTime::fromMSecsSinceEpoch(msecs, zone);
    if (interval >= 28 * kDay) {
        return time.toString(u"yyyy-MM-dd"_s);
    }
    // The label where a day starts shows the date, so a range over several days stays readable.
    if (interval >= kDay || time.time() == QTime(0, 0)) {
        return time.toString(u"MMM d"_s);
    }
    return time.toString(interval < kMinute ? u"HH:mm:ss"_s : u"HH:mm"_s);
}

QString HistogramWidget::toolTipText(const Histogram& histogram, int bar, const QTimeZone& zone) {
    if (bar < 0 || bar >= histogram.bars.size()) {
        return {};
    }
    const qint64 from = histogram.start + bar * histogram.step;
    const qint64 to = from + histogram.step;
    // The end of a bar that ends on the day it starts needs no date.
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(from, zone);
    const QDateTime finish = QDateTime::fromMSecsSinceEpoch(to, zone);
    const QString end =
        start.date() == finish.date() ? finish.toString(u"HH:mm:ss"_s) : Format::dateTime(to, zone);
    QString text = u"<b>"_s + Format::dateTime(from, zone) + u' ' + kEnDash + u' ' + end + u"</b><br>"_s;

    const LevelCounts& counts = histogram.bars[bar];
    const qint64 total = totalOf(counts);
    text += total == 1 ? tr("1 record") : tr("%1 records").arg(Format::count(total));
    if (total > 0) {
        text += u"<table cellspacing=\"0\" cellpadding=\"1\">"_s;
        for (const Level level : kStackOrder) {
            if (const qint64 count = countOf(counts, level); count > 0) {
                text +=
                    u"<tr><td><span style=\"color:%1\">%2</span> %3</td><td align=\"right\">&nbsp;&nbsp;%4</td></tr>"_s
                        .arg(Theme::levelColor(level).name(), QString(kSquare), levelName(level),
                             Format::count(count));
            }
        }
        text += u"</table>"_s;
    }
    return text + u"<i>"_s + tr("Drag across bars to zoom in; Esc goes back.").toHtmlEscaped() + u"</i>"_s;
}

bool HistogramWidget::event(QEvent* event) {
    if (event->type() != QEvent::ToolTip) {
        return QWidget::event(event);
    }
    const auto* help = static_cast<QHelpEvent*>(event);
    const int bar = m_dragging ? -1 : barAt(help->pos().x());
    if (bar < 0) {
        QToolTip::hideText();
        event->ignore();
    } else {
        showToolTip(help->globalPos(), bar);
    }
    return true;
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    painter.setPen(axisColor(60));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!m_hasResult) {
        return;
    }
    if (m_histogram.bars.isEmpty()) {
        drawMessage(painter, rect(), tr("No matching records"));
        return;
    }
    const QRectF plot = plotArea();
    if (m_hoveredBar >= 0 && !m_dragging) {
        QColor hover = palette().color(QPalette::Highlight);
        hover.setAlpha(kHoverAlpha);
        painter.fillRect(barColumn(plot, m_hoveredBar), hover);
    }
    drawBars(painter, plot);
    if (m_dragging) {
        drawSelection(painter, plot);
    }
    drawAxes(painter, plot);
    if (m_maxTotal == 0) {
        drawMessage(painter, plot, tr("No matching records in this period"));
    }
}

void HistogramWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_histogram.bars.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_pressX = event->position().x();
    m_anchorTime = timeAt(m_pressX);
    m_currentTime = m_anchorTime;
    QToolTip::hideText();
    update();
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* event) {
    const qreal x = event->position().x();
    if (m_dragging) {
        m_currentTime = timeAt(x);
    }
    m_hoveredBar = barAt(x);
    update();
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_dragging) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    update();
    // A click is no selection: it only gives the histogram the focus.
    const qreal x = event->position().x();
    if (qAbs(x - m_pressX) < QApplication::startDragDistance() || m_histogram.bars.isEmpty()) {
        return;
    }
    const auto [from, to] = selectionPeriod(m_histogram, m_anchorTime, timeAt(x));
    emit periodSelected(from, to);
}

void HistogramWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() != Qt::Key_Escape) {
        QWidget::keyPressEvent(event);
        return;
    }
    // Esc first drops a selection that is still being dragged, then steps back through the zooms.
    if (m_dragging) {
        m_dragging = false;
        update();
        return;
    }
    emit previousPeriodRequested();
}

void HistogramWidget::leaveEvent(QEvent* event) {
    m_hoveredBar = -1;
    update();
    QWidget::leaveEvent(event);
}

QRectF HistogramWidget::plotArea() const {
    const QFontMetrics metrics = fontMetrics();
    // Room for the count at the top of the scale on the left and for the time labels below.
    const qreal left = metrics.horizontalAdvance(Format::count(m_maxTotal)) + 2 * kPadding;
    const qreal top = metrics.height() / 2.0 + kPadding / 2.0;
    const qreal bottom = metrics.height() + kTickLength + kPadding;
    return {left, top, qMax(1.0, width() - left - kPadding), qMax(1.0, height() - top - bottom)};
}

QRectF HistogramWidget::barColumn(const QRectF& plot, int bar) const {
    const qreal width = plot.width() / m_histogram.bars.size();
    return {plot.left() + bar * width, plot.top(), width, plot.height()};
}

int HistogramWidget::barAt(qreal x) const {
    const QRectF plot = plotArea();
    const qsizetype count = m_histogram.bars.size();
    if (count == 0 || x < plot.left() || x >= plot.right()) {
        return -1;
    }
    return static_cast<int>(
        qMin<qsizetype>(static_cast<qsizetype>((x - plot.left()) / plot.width() * count), count - 1));
}

qint64 HistogramWidget::timeAt(qreal x) const {
    const QRectF plot = plotArea();
    const qint64 span = m_histogram.step * m_histogram.bars.size();
    if (span <= 0) {
        return m_histogram.start;
    }
    const qreal share = qBound(0.0, (x - plot.left()) / plot.width(), 1.0);
    return m_histogram.start + qMin(static_cast<qint64>(share * static_cast<qreal>(span)), span - 1);
}

void HistogramWidget::drawBars(QPainter& painter, const QRectF& plot) const {
    if (m_maxTotal == 0) {
        return;
    }
    const qsizetype count = m_histogram.bars.size();
    const qreal barWidth = plot.width() / count;
    const int gap = barWidth >= kMinGappedBarWidth ? 1 : 0;
    const int bottom = qRound(plot.bottom());
    for (qsizetype i = 0; i < count; ++i) {
        const LevelCounts& counts = m_histogram.bars[i];
        const qint64 total = totalOf(counts);
        if (total == 0) {
            continue;
        }
        const int left = qRound(plot.left() + i * barWidth);
        const int right = qMax(left + 1, qRound(plot.left() + (i + 1) * barWidth) - gap);
        // Each level's top comes from the running sum, so rounding never leaves a gap between two levels.
        qint64 sum = 0;
        int below = bottom;
        for (const Level level : kStackOrder) {
            const qint64 levelCount = countOf(counts, level);
            if (levelCount == 0) {
                continue;
            }
            sum += levelCount;
            int top = bottom - qRound(static_cast<qreal>(sum) * plot.height() / m_maxTotal);
            // A bar with records is never flat, however small its share.
            if (sum == total) {
                top = qMin(top, bottom - 1);
            }
            if (top < below) {
                painter.fillRect(QRect(left, top, right - left, below - top), Theme::levelColor(level));
                below = top;
            }
        }
    }
}

void HistogramWidget::drawSelection(QPainter& painter, const QRectF& plot) const {
    const auto [from, to] = selectionPeriod(m_histogram, m_anchorTime, m_currentTime);
    const auto span = static_cast<qreal>(m_histogram.step * m_histogram.bars.size());
    const qreal left = plot.left() + static_cast<qreal>(from - m_histogram.start) / span * plot.width();
    const qreal right = plot.left() + static_cast<qreal>(to - m_histogram.start) / span * plot.width();
    const QRectF band(QPointF(left, plot.top()), QPointF(right, plot.bottom()));
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(kSelectionAlpha);
    painter.fillRect(band, fill);
    painter.setPen(axisColor(200));
    painter.drawLine(band.topLeft(), band.bottomLeft());
    painter.drawLine(band.topRight(), band.bottomRight());
}

void HistogramWidget::drawAxes(QPainter& painter, const QRectF& plot) const {
    const QFontMetrics metrics = fontMetrics();
    // The scale: a faint line at the height of the fullest bar, labeled with its count, and the time axis.
    painter.setPen(axisColor(40));
    painter.drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.right(), plot.top()));
    painter.setPen(axisColor(110));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    painter.setPen(axisColor(170));
    if (m_maxTotal > 0) {
        painter.drawText(
            QRectF(0, plot.top() - metrics.height() / 2.0, plot.left() - kPadding, metrics.height()),
            Qt::AlignRight | Qt::AlignVCenter, Format::count(m_maxTotal));
    }

    const qreal barWidth = plot.width() / m_histogram.bars.size();
    // Room for the widest label there is, a full date.
    const qreal minSpacing = metrics.horizontalAdvance(u"0000-00-00"_s) + 2 * kPadding;
    const qint64 interval = labelInterval(m_histogram.step, barWidth, minSpacing);
    qreal lastLabelEnd = -kPadding;
    for (const int boundary : labelBoundaries(m_histogram, interval, m_zone)) {
        const qreal x = plot.left() + boundary * barWidth;
        painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + kTickLength));
        const QString text = labelText(m_histogram.start + boundary * m_histogram.step, interval, m_zone);
        const qreal textWidth = metrics.horizontalAdvance(text);
        // Centered under its tick but kept inside the widget, and never on top of the label before it.
        const qreal left = qBound(0.0, x - textWidth / 2, qMax(0.0, width() - textWidth));
        if (left < lastLabelEnd + kPadding) {
            continue;
        }
        painter.drawText(QRectF(left, plot.bottom() + kTickLength, textWidth, metrics.height()),
                         Qt::AlignCenter, text);
        lastLabelEnd = left + textWidth;
    }
}

void HistogramWidget::drawMessage(QPainter& painter, const QRectF& area, const QString& text) const {
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(area, Qt::AlignCenter, text);
}

void HistogramWidget::showToolTip(const QPoint& globalPosition, int bar) {
    QToolTip::showText(globalPosition, toolTipText(m_histogram, bar, m_zone), this,
                       barColumn(plotArea(), bar).toAlignedRect());
}

QColor HistogramWidget::axisColor(int alpha) const {
    QColor color = palette().color(QPalette::Text);
    color.setAlpha(alpha);
    return color;
}
