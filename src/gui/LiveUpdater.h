#pragma once

#include <QElapsedTimer>
#include <QObject>

class QTimer;

/// Decides when live mode refreshes the search (DESIGN.md section 6.6): written records ask for a refresh,
/// which comes at most once per interval, never while a search is running, and not while paused (the user
/// scrolled the table down or selected a record). Lives in the GUI thread.
class LiveUpdater : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultIntervalMs = 1000;

    explicit LiveUpdater(int intervalMs = kDefaultIntervalMs, QObject* parent = nullptr);

    bool isEnabled() const;
    /// Turning live mode on asks for a refresh, so records written while it was off show up.
    void setEnabled(bool enabled);

    bool isPaused() const;
    void setPaused(bool paused);

    /// Records were written to the database (Ingestor::recordsWritten()).
    void recordsWritten();

    /// A search started, whoever started it: it shows every record written before it.
    void searchStarted();
    /// The result of the latest search arrived (or it failed).
    void searchFinished();

signals:
    /// Time to start a search.
    void refreshDue();
    /// isEnabled() or isPaused() changed.
    void stateChanged();

private:
    void schedule();
    void fire();

    QTimer* m_timer;
    QElapsedTimer m_sinceSearchStarted;
    int m_intervalMs;
    bool m_enabled = false;
    bool m_paused = false;
    bool m_pending = false; // records were written that no search has seen yet
    bool m_searching = false;
};
