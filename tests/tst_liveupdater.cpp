#include "tst_liveupdater.h"

#include "LiveUpdater.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

namespace {

// Short enough to keep the tests fast, long enough for the timers to tell "now" from "later".
constexpr int kIntervalMs = 100;
constexpr int kWaitMs = 5 * kIntervalMs;

// Starts live mode and lets its first refresh, the catch-up one, go through a search.
bool startLive(LiveUpdater& live, QSignalSpy& refreshes) {
    live.setEnabled(true);
    if (!refreshes.wait(kWaitMs)) {
        return false;
    }
    live.searchStarted();
    live.searchFinished();
    return true;
}

} // namespace

void TestLiveUpdater::staysQuietWhileDisabled() {
    LiveUpdater live(kIntervalMs);
    QSignalSpy refreshes(&live, &LiveUpdater::refreshDue);
    live.recordsWritten();
    QTest::qWait(2 * kIntervalMs);
    QCOMPARE(refreshes.count(), 0);
}

void TestLiveUpdater::refreshesWhenEnabled() {
    LiveUpdater live(kIntervalMs);
    QSignalSpy refreshes(&live, &LiveUpdater::refreshDue);
    // Records written while live mode was off show up at once.
    live.setEnabled(true);
    QVERIFY(refreshes.wait(kWaitMs));
    QCOMPARE(refreshes.count(), 1);
}

void TestLiveUpdater::refreshesAtMostOncePerInterval() {
    LiveUpdater live(kIntervalMs);
    QSignalSpy refreshes(&live, &LiveUpdater::refreshDue);
    QVERIFY(startLive(live, refreshes));

    QElapsedTimer clock;
    clock.start();
    live.searchStarted();
    live.searchFinished();
    for (int i = 0; i < 20; ++i) {
        live.recordsWritten();
    }
    QVERIFY(refreshes.wait(kWaitMs));
    QCOMPARE(refreshes.count(), 2);
    // Coarse timers may fire up to 5% early.
    QVERIFY2(clock.elapsed() >= kIntervalMs * 9 / 10, qPrintable(QString::number(clock.elapsed())));
    QTest::qWait(2 * kIntervalMs);
    QCOMPARE(refreshes.count(), 2);
}

void TestLiveUpdater::waitsForTheRunningSearch() {
    LiveUpdater live(kIntervalMs);
    QSignalSpy refreshes(&live, &LiveUpdater::refreshDue);
    QVERIFY(startLive(live, refreshes));

    live.searchStarted();
    live.recordsWritten();
    QTest::qWait(3 * kIntervalMs);
    QCOMPARE(refreshes.count(), 1);
    live.searchFinished();
    QVERIFY(refreshes.wait(kWaitMs));
    QCOMPARE(refreshes.count(), 2);
}

void TestLiveUpdater::waitsWhilePaused() {
    LiveUpdater live(kIntervalMs);
    QSignalSpy refreshes(&live, &LiveUpdater::refreshDue);
    QVERIFY(startLive(live, refreshes));

    live.setPaused(true);
    live.recordsWritten();
    QTest::qWait(3 * kIntervalMs);
    QCOMPARE(refreshes.count(), 1);
    live.setPaused(false);
    QVERIFY(refreshes.wait(kWaitMs));
    QCOMPARE(refreshes.count(), 2);
}

void TestLiveUpdater::reportsStateChanges() {
    LiveUpdater live(kIntervalMs);
    QSignalSpy changes(&live, &LiveUpdater::stateChanged);
    live.setEnabled(true);
    live.setEnabled(true);
    live.setPaused(true);
    QVERIFY(live.isEnabled());
    QVERIFY(live.isPaused());
    live.setPaused(false);
    live.setEnabled(false);
    QCOMPARE(changes.count(), 4);
    QVERIFY(!live.isEnabled());
}
