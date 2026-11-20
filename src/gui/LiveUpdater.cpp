#include "LiveUpdater.h"

#include <QTimer>

LiveUpdater::LiveUpdater(int intervalMs, QObject* parent)
    : QObject(parent), m_timer(new QTimer(this)), m_intervalMs(intervalMs) {
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &LiveUpdater::fire);
}

bool LiveUpdater::isEnabled() const { return m_enabled; }

void LiveUpdater::setEnabled(bool enabled) {
    if (enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    m_pending = enabled;
    if (enabled) {
        schedule();
    } else {
        m_timer->stop();
    }
    emit stateChanged();
}

bool LiveUpdater::isPaused() const { return m_paused; }

void LiveUpdater::setPaused(bool paused) {
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (paused) {
        m_timer->stop();
    } else {
        schedule();
    }
    emit stateChanged();
}

void LiveUpdater::recordsWritten() {
    m_pending = true;
    schedule();
}

void LiveUpdater::searchStarted() {
    m_pending = false;
    m_searching = true;
    m_timer->stop();
    m_sinceSearchStarted.start();
}

void LiveUpdater::searchFinished() {
    m_searching = false;
    schedule();
}

void LiveUpdater::schedule() {
    if (!m_enabled || m_paused || m_searching || !m_pending || m_timer->isActive()) {
        return;
    }
    // The interval counts from the start of the previous search, so a steady stream of records refreshes
    // the table once per interval, not once per interval plus the search time.
    const qint64 elapsed = m_sinceSearchStarted.isValid() ? m_sinceSearchStarted.elapsed() : m_intervalMs;
    m_timer->start(static_cast<int>(qMax<qint64>(0, m_intervalMs - elapsed)));
}

void LiveUpdater::fire() {
    if (m_enabled && !m_paused && !m_searching && m_pending) {
        emit refreshDue();
    }
}
