#pragma once

#include "TcpServer.h"

#include <QWidget>

class QAction;
class QLabel;
class QTableWidget;

/// The "Receiver" panel (DESIGN.md section 2.2): the receiver's state, its start/stop button, the client
/// connections and the receive rate, as TcpServer reports them.
class ReceiverPanel : public QWidget {
    Q_OBJECT

public:
    /// The button shows startStopAction (Receiver > Start/Stop), so both always look the same.
    explicit ReceiverPanel(QAction* startStopAction, QWidget* parent = nullptr);

    void setStats(const ReceiverStats& stats);

private:
    void showConnections(const QVector<ConnectionInfo>& connections);

    QLabel* m_state;
    QTableWidget* m_connections;
    QLabel* m_rate;
};
