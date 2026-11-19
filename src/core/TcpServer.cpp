#include "TcpServer.h"

#include <QDateTime>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr auto kDefaultSource = "tcp"_L1;

// "127.0.0.1:5170", or "[::1]:5170" for IPv6.
QString endpoint(const QHostAddress& address, quint16 port) {
    const QString host = address.protocol() == QAbstractSocket::IPv6Protocol
                             ? u'[' + address.toString() + u']'
                             : address.toString();
    return host + u':' + QString::number(port);
}

} // namespace

TcpServer::TcpServer(QObject* parent)
    : QObject(parent), m_statsTimer(new QTimer(this)), m_defaultSource(kDefaultSource) {
    m_statsTimer->setInterval(kStatsIntervalMs);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        updateRate();
        emit statsChanged(stats());
    });
}

// The listening socket and the client sockets are children and go with the server; connections are removed
// before children are deleted, so no handler runs during destruction.
TcpServer::~TcpServer() = default;

void TcpServer::setDefaultSource(const QString& name) {
    const QString trimmed = name.trimmed();
    m_defaultSource = trimmed.isEmpty() ? QString(kDefaultSource) : trimmed;
}

bool TcpServer::start(const QHostAddress& address, quint16 port) {
    // A socket belongs to the thread that creates it; all of them must live in the intake thread.
    Q_ASSERT(QThread::currentThread() == thread());
    if (isListening()) {
        stop();
    }
    if (m_server == nullptr) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &TcpServer::acceptConnections);
        connect(m_server, &QTcpServer::acceptError, this, [this] {
            emit errorOccurred(tr("Cannot accept a connection: %1").arg(m_server->errorString()));
        });
    }
    m_address = address;
    m_port = port;
    m_error.clear();
    m_totalLines = 0;
    m_linesSinceStats = 0;
    m_linesPerSecond = 0;
    if (!m_server->listen(address, port)) {
        m_error = tr("Cannot listen on %1: %2").arg(endpoint(address, port), m_server->errorString());
        emit errorOccurred(m_error);
        emit statsChanged(stats());
        return false;
    }
    m_port = m_server->serverPort();
    m_statsClock.start();
    m_statsTimer->start();
    emit statsChanged(stats());
    return true;
}

void TcpServer::stop() {
    m_statsTimer->stop();
    if (m_server != nullptr) {
        m_server->close();
    }
    for (auto& entry : m_clients) {
        releaseClient(entry.second);
    }
    m_clients.clear();
    m_error.clear();
    m_linesPerSecond = 0;
    emit statsChanged(stats());
}

bool TcpServer::isListening() const { return m_server != nullptr && m_server->isListening(); }

ReceiverStats TcpServer::stats() const {
    ReceiverStats stats;
    stats.listening = isListening();
    stats.address = m_address.toString();
    stats.port = m_port;
    stats.error = m_error;
    stats.connections.reserve(static_cast<qsizetype>(m_clients.size()));
    for (const auto& entry : m_clients) {
        stats.connections.append(entry.second.info);
    }
    stats.linesPerSecond = m_linesPerSecond;
    stats.totalLines = m_totalLines;
    return stats;
}

void TcpServer::acceptConnections() {
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        if (m_clients.size() >= kMaxConnections) {
            // Over the limit: the client sees its connection closed at once.
            socket->close();
            socket->deleteLater();
            continue;
        }
        const quint64 id = m_nextClientId++;
        Client& client = m_clients[id];
        client.socket = socket;
        client.info = {id, endpoint(socket->peerAddress(), socket->peerPort()),
                       QDateTime::currentMSecsSinceEpoch(), 0};
        connect(socket, &QTcpSocket::readyRead, this, [this, id] { readClient(id); });
        connect(socket, &QTcpSocket::disconnected, this, [this, id] { dropClient(id); });
        // A failed connection normally reports disconnected() as well; this covers one that does not.
        connect(socket, &QTcpSocket::errorOccurred, this, [this, id, socket] {
            if (socket->state() == QAbstractSocket::UnconnectedState) {
                dropClient(id);
            }
        });
    }
}

void TcpServer::readClient(quint64 id) {
    const auto it = m_clients.find(id);
    if (it != m_clients.end()) {
        Client& client = it->second;
        passOn(client, client.splitter.append(client.socket->readAll()));
    }
}

void TcpServer::dropClient(quint64 id) {
    const auto it = m_clients.find(id);
    if (it != m_clients.end()) {
        releaseClient(it->second);
        m_clients.erase(it);
    }
}

void TcpServer::releaseClient(Client& client) {
    client.socket->disconnect(this);
    // Bytes already received are kept, including a last line without '\n' (the client went away mid-line).
    QVector<LineSplitter::Line> lines = client.splitter.append(client.socket->readAll());
    if (std::optional<LineSplitter::Line> last = client.splitter.finish()) {
        lines.append(std::move(*last));
    }
    passOn(client, lines);
    client.socket->close();
    client.socket->deleteLater();
}

void TcpServer::passOn(Client& client, const QVector<LineSplitter::Line>& lines) {
    if (lines.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<ReceivedLine> received;
    received.reserve(lines.size());
    for (const LineSplitter::Line& line : lines) {
        received.append({line.text, m_defaultSource, now, line.truncated});
    }
    client.info.lines += lines.size();
    m_totalLines += lines.size();
    m_linesSinceStats += lines.size();
    emit linesReceived(received);
}

void TcpServer::updateRate() {
    const qint64 elapsed = m_statsClock.restart();
    m_linesPerSecond = elapsed > 0 ? qRound64(double(m_linesSinceStats) * 1000.0 / double(elapsed)) : 0;
    m_linesSinceStats = 0;
}
