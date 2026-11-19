#pragma once

#include "Ingestor.h"
#include "TcpServer.h"

#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QThread>

/// Runs a TcpServer and an Ingestor in a thread of their own, wired the way the application wires them:
/// both are created, moved to the thread and connected, the thread starts, and then they are started in it
/// through queued calls. The public members are updated in the test thread, as GUI slots would be.
class IntakeRig : public QObject {
public:
    explicit IntakeRig(const QString& databasePath) : m_ingestor(new Ingestor), m_server(new TcpServer) {
        m_ingestor->moveToThread(&m_thread);
        m_server->moveToThread(&m_thread);
        // Each is deleted in the intake thread once the thread finishes.
        connect(&m_thread, &QThread::finished, m_ingestor, &QObject::deleteLater);
        connect(&m_thread, &QThread::finished, m_server, &QObject::deleteLater);
        // Both live in the intake thread, so lines are handed over by a direct call.
        connect(m_server, &TcpServer::linesReceived, m_ingestor, &Ingestor::addLines);
        connect(m_ingestor, &Ingestor::recordsWritten, this, [this](int count) { written += count; });
        connect(m_ingestor, &Ingestor::errorOccurred, this,
                [this](const QString& message) { errors += message; });
        connect(m_server, &TcpServer::errorOccurred, this,
                [this](const QString& message) { errors += message; });
        connect(m_server, &TcpServer::statsChanged, this, [this](const ReceiverStats& received) {
            stats = received;
            maxLinesPerSecond = qMax(maxLinesPerSecond, received.linesPerSecond);
        });
        m_thread.start();
        QMetaObject::invokeMethod(
            m_ingestor, [this, databasePath] { ingestorStarted = m_ingestor->start(databasePath); },
            Qt::BlockingQueuedConnection);
    }

    ~IntakeRig() override { shutdown(); }

    IntakeRig(const IntakeRig&) = delete;
    IntakeRig& operator=(const IntakeRig&) = delete;

    /// Starts listening on 127.0.0.1 (a free port if port is 0).
    bool listen(quint16 port = 0) {
        bool listening = false;
        QMetaObject::invokeMethod(
            m_server,
            [this, port, &listening] {
                listening = m_server->start(QHostAddress::LocalHost, port);
                m_port = m_server->stats().port;
            },
            Qt::BlockingQueuedConnection);
        return listening;
    }

    quint16 port() const { return m_port; }

    /// The application's shutdown order: the server passes on unfinished lines, the ingestor writes them
    /// and closes the database, then the thread ends and deletes both objects.
    void shutdown() {
        if (!m_thread.isRunning()) {
            return;
        }
        QMetaObject::invokeMethod(
            m_server,
            [this] {
                m_server->stop();
                m_ingestor->stop();
            },
            Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }

    bool ingestorStarted = false;
    qint64 written = 0;
    QStringList errors;
    ReceiverStats stats;
    qint64 maxLinesPerSecond = 0;

private:
    QThread m_thread;
    Ingestor* m_ingestor; // owned by the intake thread: deleted when it finishes
    TcpServer* m_server;  // likewise
    quint16 m_port = 0;
};
