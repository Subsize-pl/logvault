#pragma once

#include <QObject>

/// Checks when live mode refreshes: at most once per interval, never during a search, not while paused.
class TestLiveUpdater : public QObject {
    Q_OBJECT

private slots:
    void staysQuietWhileDisabled();
    void refreshesWhenEnabled();
    void refreshesAtMostOncePerInterval();
    void waitsForTheRunningSearch();
    void waitsWhilePaused();
    void reportsStateChanges();
};
