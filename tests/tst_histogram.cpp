#include "tst_histogram.h"

#include "Format.h"
#include "HistogramWidget.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QTest>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kSecond = 1000;
constexpr qint64 kMinute = 60 * kSecond;
constexpr qint64 kHour = 60 * kMinute;
constexpr qint64 kDay = 24 * kHour;

// 2026-11-20 10:00:00 UTC, a Friday.
constexpr qint64 kT = 1'795'168'800'000;

using Period = std::pair<qint64, qint64>;

// Bars of one minute from kT, with a few records in each.
Histogram minuteBars(int count) {
    Histogram histogram;
    histogram.start = kT;
    histogram.step = kMinute;
    histogram.bars.resize(count);
    for (int i = 0; i < count; ++i) {
        histogram.bars[i][static_cast<int>(Level::Info)] = 10 + i;
        histogram.bars[i][static_cast<int>(Level::Error)] = i % 3;
    }
    return histogram;
}

} // namespace

void TestHistogram::selectionCoversWholeBars() {
    const Histogram histogram = minuteBars(10);
    // Times in bars 2 and 5 give bars 2 to 5, in either order.
    const Period period = HistogramWidget::selectionPeriod(histogram, kT + 2 * kMinute + 5 * kSecond,
                                                           kT + 5 * kMinute + 59 * kSecond);
    QCOMPARE(period, Period(kT + 2 * kMinute, kT + 6 * kMinute));
    QCOMPARE(HistogramWidget::selectionPeriod(histogram, kT + 5 * kMinute, kT + 2 * kMinute), period);
    // Within one bar: that bar.
    QCOMPARE(HistogramWidget::selectionPeriod(histogram, kT + 30 * kSecond, kT + 31 * kSecond),
             Period(kT, kT + kMinute));
    // Outside the bars: the nearest ones.
    QCOMPARE(HistogramWidget::selectionPeriod(histogram, kT - kHour, kT + kDay),
             Period(kT, kT + 10 * kMinute));
    QCOMPARE(HistogramWidget::selectionPeriod(Histogram(), kT, kT + 1), Period(0, 0));
}

void TestHistogram::labelIntervalFitsTheBars_data() {
    QTest::addColumn<qint64>("step");
    QTest::addColumn<qreal>("barWidth");
    QTest::addColumn<qint64>("expected");
    // Labels need 70 pixels each.
    QTest::newRow("1 s, wide bars") << kSecond << 100.0 << kSecond;
    QTest::newRow("1 s") << kSecond << 10.0 << 10 * kSecond;
    QTest::newRow("5 s") << 5 * kSecond << 4.0 << 2 * kMinute;
    QTest::newRow("1 min") << kMinute << 5.0 << 15 * kMinute;
    QTest::newRow("3 h") << 3 * kHour << 20.0 << 12 * kHour;
    QTest::newRow("1 day") << kDay << 9.0 << 14 * kDay;
    QTest::newRow("7 days") << 7 * kDay << 8.0 << 91 * kDay;
    QTest::newRow("30 days") << 30 * kDay << 10.0 << 360 * kDay;
    QTest::newRow("30 days, narrow bars") << 30 * kDay << 2.0 << 35 * 30 * kDay;
    QTest::newRow("90 days") << 90 * kDay << 10.0 << 7 * 90 * kDay;
}

void TestHistogram::labelIntervalFitsTheBars() {
    QFETCH(qint64, step);
    QFETCH(qreal, barWidth);
    QFETCH(qint64, expected);
    const qint64 interval = HistogramWidget::labelInterval(step, barWidth, 70);
    QCOMPARE(interval, expected);
    QCOMPARE(interval % step, 0);
    QVERIFY(static_cast<qreal>(interval / step) * barWidth >= 70);
}

void TestHistogram::labelsSitOnRoundTimes() {
    // Every 5 minutes; every time zone is a whole number of quarter hours away from UTC.
    QCOMPARE(HistogramWidget::labelBoundaries(minuteBars(30), 5 * kMinute),
             (QVector<int>{0, 5, 10, 15, 20, 25, 30}));

    // Bars of a day start at UTC midnight. Weekly labels then fall on Thursdays (1970-01-01 was one),
    // whatever the local time zone: 2026-11-26 is the 7th boundary from 2026-11-20.
    Histogram days;
    days.start = kT - 10 * kHour;
    days.step = kDay;
    days.bars.resize(30);
    QCOMPARE(HistogramWidget::labelBoundaries(days, 7 * kDay), (QVector<int>{6, 13, 20, 27}));

    QVERIFY(HistogramWidget::labelBoundaries(Histogram(), kMinute).isEmpty());
}

void TestHistogram::labelTextMatchesTheInterval() {
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(kT);
    QCOMPARE(HistogramWidget::labelText(kT, kSecond), local.toString(u"HH:mm:ss"_s));
    QCOMPARE(HistogramWidget::labelText(kT, 5 * kMinute), local.toString(u"HH:mm"_s));
    QCOMPARE(HistogramWidget::labelText(kT, kDay), local.toString(u"MMM d"_s));
    QCOMPARE(HistogramWidget::labelText(kT, 30 * kDay), local.toString(u"yyyy-MM-dd"_s));
    // A label at local midnight shows the day instead of 00:00.
    const qint64 midnight = local.date().startOfDay().toMSecsSinceEpoch();
    QCOMPARE(HistogramWidget::labelText(midnight, kHour), local.toString(u"MMM d"_s));
    // Month names are English, as the rest of the UI.
    QVERIFY(HistogramWidget::labelText(kT, kDay).startsWith(u"Nov"_s));
}

void TestHistogram::toolTipShowsIntervalAndLevels() {
    Histogram histogram = minuteBars(3);
    histogram.bars[1] = {};
    histogram.bars[1][static_cast<int>(Level::Info)] = 1200;
    histogram.bars[1][static_cast<int>(Level::Error)] = 12;
    histogram.bars[1][static_cast<int>(Level::Fatal)] = 1;
    histogram.bars[2] = {};

    const QString text = HistogramWidget::toolTipText(histogram, 1);
    QVERIFY2(text.contains(Format::dateTime(kT + kMinute)), qPrintable(text));
    QVERIFY(text.contains(QDateTime::fromMSecsSinceEpoch(kT + 2 * kMinute).toString(u"HH:mm:ss"_s)));
    QVERIFY(text.contains(u"1,213 records"_s));
    QVERIFY(text.contains(u"1,200"_s));
    QVERIFY(!text.contains(u"DEBUG"_s));
    // The most severe levels first.
    QVERIFY(text.indexOf(u"FATAL"_s) < text.indexOf(u"ERROR"_s));
    QVERIFY(text.indexOf(u"ERROR"_s) < text.indexOf(u"INFO"_s));

    QVERIFY(HistogramWidget::toolTipText(histogram, 2).contains(u"0 records"_s));
    QVERIFY(HistogramWidget::toolTipText(histogram, 3).isEmpty());
}

void TestHistogram::dragSelectsAPeriod() {
    HistogramWidget widget;
    widget.resize(800, 120);
    widget.setHistogram(minuteBars(10));
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QSignalSpy selected(&widget, &HistogramWidget::periodSelected);

    const int y = widget.height() / 2;
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(300, y));
    QTest::mouseMove(&widget, QPoint(450, y));
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(600, y));
    QCOMPARE(selected.count(), 1);
    const auto from = selected.first().at(0).toLongLong();
    const auto to = selected.first().at(1).toLongLong();
    // Whole bars, several of them, inside the histogram.
    QVERIFY(from >= kT && to <= kT + 10 * kMinute);
    QCOMPARE((from - kT) % kMinute, 0);
    QCOMPARE((to - kT) % kMinute, 0);
    QVERIFY(to - from >= 2 * kMinute);

    // Dragging the other way selects the same bars.
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(600, y));
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(300, y));
    QCOMPARE(selected.count(), 2);
    QCOMPARE(selected.at(1), selected.at(0));
}

void TestHistogram::clickAndEscapeSelectNothing() {
    HistogramWidget widget;
    widget.resize(800, 120);
    widget.setHistogram(minuteBars(10));
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QSignalSpy selected(&widget, &HistogramWidget::periodSelected);
    QSignalSpy back(&widget, &HistogramWidget::previousPeriodRequested);
    const int y = widget.height() / 2;

    // A click selects nothing.
    QTest::mouseClick(&widget, Qt::LeftButton, {}, QPoint(400, y));
    QCOMPARE(selected.count(), 0);

    // Esc during a drag drops it; the release that follows selects nothing either.
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(300, y));
    QTest::keyClick(&widget, Qt::Key_Escape);
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(600, y));
    QCOMPARE(selected.count(), 0);
    QCOMPARE(back.count(), 0);

    // Esc otherwise asks for the period before.
    QTest::keyClick(&widget, Qt::Key_Escape);
    QCOMPARE(back.count(), 1);

    // Without bars there is nothing to drag.
    widget.setHistogram(Histogram());
    QTest::mousePress(&widget, Qt::LeftButton, {}, QPoint(300, y));
    QTest::mouseRelease(&widget, Qt::LeftButton, {}, QPoint(600, y));
    QCOMPARE(selected.count(), 0);
}
