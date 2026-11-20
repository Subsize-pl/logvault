#include "tst_filterbar.h"

#include "FilterBar.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kNow = 1'795'168'800'000;
constexpr qint64 kHour = 3'600'000;

SearchRequest requestOf(const FilterBar& bar, qint64 now = kNow) {
    SearchRequest request;
    bar.fillRequest(request, now);
    return request;
}

QToolButton* levelButton(const FilterBar& bar, const QString& text) {
    for (QToolButton* button : bar.findChildren<QToolButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

// Picks a period as the user does: activated() comes only from user interaction.
void choosePeriod(FilterBar& bar, const QString& text) {
    auto* period = bar.findChild<QComboBox*>();
    const int index = period->findText(text);
    period->setCurrentIndex(index);
    emit period->activated(index);
}

} // namespace

void TestFilterBar::startsWithEverything() {
    FilterBar bar;
    const SearchRequest request = requestOf(bar);
    QCOMPARE(request.levels.size(), kLevelCount);
    QCOMPARE(request.from, 0);
    QCOMPARE(request.to, 0);
    QVERIFY(request.text.isEmpty());
}

void TestFilterBar::togglesLevels() {
    FilterBar bar;
    QSignalSpy searches(&bar, &FilterBar::searchRequested);
    QToolButton* debug = levelButton(bar, u"DEBUG"_s);
    QVERIFY(debug);
    QTest::mouseClick(debug, Qt::LeftButton);
    QCOMPARE(searches.count(), 1);
    QList<Level> levels = requestOf(bar).levels;
    QCOMPARE(levels.size(), kLevelCount - 1);
    QVERIFY(!levels.contains(Level::Debug));

    bar.hideLevel(Level::Unknown);
    levels = requestOf(bar).levels;
    QCOMPARE(levels.size(), kLevelCount - 2);
    QVERIFY(!levels.contains(Level::Unknown));
    QCOMPARE(searches.count(), 2);
}

void TestFilterBar::ctrlClickKeepsOnlyOneLevel() {
    FilterBar bar;
    QSignalSpy searches(&bar, &FilterBar::searchRequested);
    QToolButton* error = levelButton(bar, u"ERROR"_s);
    QVERIFY(error);
    QTest::mouseClick(error, Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(requestOf(bar).levels, QList<Level>{Level::Error});
    QVERIFY(error->isChecked());
    QCOMPARE(searches.count(), 1);

    bar.showOnlyLevel(Level::Warn);
    QCOMPARE(requestOf(bar).levels, QList<Level>{Level::Warn});
}

void TestFilterBar::relativePeriodEndsNow() {
    FilterBar bar;
    QSignalSpy searches(&bar, &FilterBar::searchRequested);
    choosePeriod(bar, u"Last hour"_s);
    QCOMPARE(searches.count(), 1);
    SearchRequest request = requestOf(bar);
    QCOMPARE(request.from, kNow - kHour);
    QCOMPARE(request.to, kNow + 1);
    // Counted again from the time of every search.
    request = requestOf(bar, kNow + 5000);
    QCOMPARE(request.from, kNow + 5000 - kHour);

    choosePeriod(bar, u"All time"_s);
    request = requestOf(bar);
    QCOMPARE(request.from, 0);
    QCOMPARE(request.to, 0);
}

void TestFilterBar::customPeriodIsKept() {
    FilterBar bar;
    QSignalSpy searches(&bar, &FilterBar::searchRequested);
    bar.zoomToPeriod(kNow - 2 * kHour, kNow - kHour);
    QCOMPARE(searches.count(), 1);
    const SearchRequest request = requestOf(bar, kNow + 10 * kHour);
    QCOMPARE(request.from, kNow - 2 * kHour);
    QCOMPARE(request.to, kNow - kHour);
    QVERIFY(bar.findChild<QComboBox*>()->currentText().contains(u" to "_s));
}

void TestFilterBar::zoomsBackThroughEarlierPeriods() {
    FilterBar bar;
    QSignalSpy searches(&bar, &FilterBar::searchRequested);
    choosePeriod(bar, u"Last hour"_s);
    bar.zoomToPeriod(kNow - 40 * 60'000, kNow - 20 * 60'000);
    bar.zoomToPeriod(kNow - 30 * 60'000, kNow - 25 * 60'000);
    // The same period again is no zoom.
    bar.zoomToPeriod(kNow - 30 * 60'000, kNow - 25 * 60'000);
    QCOMPARE(searches.count(), 3);
    QCOMPARE(requestOf(bar).from, kNow - 30 * 60'000);

    // Esc goes back one zoom at a time, to the relative period at last, which counts from now again.
    QVERIFY(bar.restorePreviousPeriod());
    SearchRequest request = requestOf(bar);
    QCOMPARE(request.from, kNow - 40 * 60'000);
    QCOMPARE(request.to, kNow - 20 * 60'000);
    QVERIFY(bar.restorePreviousPeriod());
    request = requestOf(bar, kNow + 5000);
    QCOMPARE(request.from, kNow + 5000 - kHour);
    QCOMPARE(request.to, kNow + 5000 + 1);
    QCOMPARE(bar.findChild<QComboBox*>()->currentText(), u"Last hour"_s);
    QCOMPARE(searches.count(), 5);
    QVERIFY(!bar.restorePreviousPeriod());
    QCOMPARE(searches.count(), 5);
}

void TestFilterBar::choosingAPeriodForgetsZooms() {
    FilterBar bar;
    bar.zoomToPeriod(kNow - 2 * kHour, kNow - kHour);
    choosePeriod(bar, u"All time"_s);
    QVERIFY(!bar.restorePreviousPeriod());
    QCOMPARE(requestOf(bar).from, 0);

    // Zooms beyond the limit drop the oldest ones.
    for (int i = 0; i < FilterBar::kZoomHistorySize + 5; ++i) {
        bar.zoomToPeriod(kNow - kHour + i, kNow);
    }
    int restored = 0;
    while (bar.restorePreviousPeriod()) {
        ++restored;
    }
    QCOMPARE(restored, FilterBar::kZoomHistorySize);
    QCOMPARE(requestOf(bar).from, kNow - kHour + 4);
}

void TestFilterBar::addsSearchTermsAndRemembersThem() {
    FilterBar bar;
    QSignalSpy searches(&bar, &FilterBar::searchRequested);
    bar.addSearchTerm(u"provider:\"bepaid\""_s);
    bar.addSearchTerm(u"-env:\"test\""_s);
    QCOMPARE(searches.count(), 2);
    QCOMPARE(requestOf(bar).text, u"provider:\"bepaid\" -env:\"test\""_s);

    const QAbstractItemModel* history = bar.findChild<QLineEdit*>()->completer()->model();
    QCOMPARE(history->rowCount(), 2);
    QCOMPARE(history->index(0, 0).data().toString(), u"provider:\"bepaid\" -env:\"test\""_s);

    // Emptying the line searches at once.
    bar.findChild<QLineEdit*>()->clear();
    QCOMPARE(searches.count(), 3);
    QVERIFY(requestOf(bar).text.isEmpty());
}

void TestFilterBar::keepsTwentySearchesWithoutRepeats() {
    FilterBar bar;
    auto* line = bar.findChild<QLineEdit*>();
    for (int i = 0; i < 25; ++i) {
        line->setText(u"search %1"_s.arg(i));
        QTest::keyClick(line, Qt::Key_Return);
    }
    line->setText(u"search 10"_s);
    QTest::keyClick(line, Qt::Key_Return);

    const QAbstractItemModel* history = line->completer()->model();
    QCOMPARE(history->rowCount(), FilterBar::kHistorySize);
    QCOMPARE(history->index(0, 0).data().toString(), u"search 10"_s);
    QCOMPARE(history->index(1, 0).data().toString(), u"search 24"_s);
    for (int row = 1; row < history->rowCount(); ++row) {
        QVERIFY(history->index(row, 0).data().toString() != u"search 10"_s);
    }
}
