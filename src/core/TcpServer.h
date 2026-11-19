#pragma once

#include "LineParser.h"
#include "LineSplitter.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVector>

#include <map>

class QTcpServer;
class QTcpSocket;
class QTimer;

/// One client connection, as the Receiver panel lists it.
struct ConnectionInfo {
    quint64 id = 0;         // unique while the application runs
    QString address;        // the client's "address:port"
    qint64 connectedAt = 0; // ms since 1970-01-01 UTC
    qint64 lines = 0;       // lines received on this connection
};

/// The state of the receiver, as the Receiver panel shows it.
struct ReceiverStats {
    bool listening = false;
    QString address;                     // the address it listens on, or failed to
    quint16 port = 0;                    // the port in use (the one picked when started with 0)
    QString error;                       // why the last start failed; empty if it did not
    QVector<ConnectionInfo> connections; // in connection order
    qint64 linesPerSecond = 0;           // over the last stats interval
    qint64 totalLines = 0;               // since the last start
};

/// Accepts TCP connections and splits each client's byte stream into lines (DESIGN.md section 5.2).
/// Create it in any thread, move it to the intake thread and call every method there: the listening and
/// client sockets are created in that thread, and all signals are emitted from it.
class TcpServer : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxConnections = 50;
    static constexpr int kStatsIntervalMs = 1000;

    explicit TcpServer(QObject* parent = nullptr);
    ~TcpServer() override;

    /// The source of lines that do not name their own; a blank name means the default, "tcp".
    void setDefaultSource(const QString& name);

    /// Starts listening, stopping a running session first. Port 0 picks a free port (see stats()).
    /// Returns false and emits errorOccurred() if the address and port cannot be bound.
    bool start(const QHostAddress& address, quint16 port);

    /// Stops listening and closes every connection; unfinished lines are passed on through linesReceived().
    void stop();

    bool isListening() const;
    ReceiverStats stats() const;

signals:
    /// Complete lines from one client, in the order they arrived.
    void linesReceived(const QVector<ReceivedLine>& lines);
    /// Emitted once per second while listening, and on every start() and stop().
    void statsChanged(const ReceiverStats& stats);
    void errorOccurred(const QString& message);

private:
    struct Client {
        QTcpSocket* socket = nullptr;
        ConnectionInfo info;
        LineSplitter splitter;
    };

    void acceptConnections();
    void readClient(quint64 id);
    void dropClient(quint64 id);
    void releaseClient(Client& client);
    void passOn(Client& client, const QVector<LineSplitter::Line>& lines);
    void updateRate();

    QTcpServer* m_server = nullptr; // created by the first start(), in the intake thread
    QTimer* m_statsTimer;
    std::map<quint64, Client> m_clients; // ordered by id, which is the connection order
    quint64 m_nextClientId = 1;
    QString m_defaultSource;
    QHostAddress m_address;
    quint16 m_port = 0;
    QString m_error;
    qint64 m_totalLines = 0;
    qint64 m_linesSinceStats = 0;
    qint64 m_linesPerSecond = 0;
    QElapsedTimer m_statsClock;
};
