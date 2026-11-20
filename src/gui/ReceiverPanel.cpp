#include "ReceiverPanel.h"

#include "Format.h"
#include "Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum Column { AddressColumn, ConnectedColumn, LinesColumn, ColumnCount };

constexpr QChar kDot(0x25CF);

// Updates a cell in place, so the table keeps its scroll position and selection between updates.
void setCell(QTableWidget* table, int row, int column, const QString& text) {
    QTableWidgetItem* item = table->item(row, column);
    if (item == nullptr) {
        item = new QTableWidgetItem;
        item->setTextAlignment(column == LinesColumn ? Qt::AlignRight | Qt::AlignVCenter
                                                     : Qt::AlignLeft | Qt::AlignVCenter);
        table->setItem(row, column, item);
    }
    item->setText(text);
}

} // namespace

ReceiverPanel::ReceiverPanel(QAction* startStopAction, QWidget* parent)
    : QWidget(parent), m_state(new QLabel), m_connections(new QTableWidget(0, ColumnCount)),
      m_rate(new QLabel) {
    m_state->setWordWrap(true);
    m_state->setTextFormat(Qt::RichText);

    auto* button = new QToolButton;
    button->setDefaultAction(startStopAction);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_connections->setHorizontalHeaderLabels({tr("Client"), tr("Connected"), tr("Lines")});
    m_connections->verticalHeader()->hide();
    m_connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_connections->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_connections->setWordWrap(false);
    m_connections->horizontalHeader()->setSectionResizeMode(AddressColumn, QHeaderView::Stretch);
    m_connections->horizontalHeader()->setSectionResizeMode(ConnectedColumn, QHeaderView::ResizeToContents);
    m_connections->horizontalHeader()->setSectionResizeMode(LinesColumn, QHeaderView::ResizeToContents);

    auto* top = new QHBoxLayout;
    top->addWidget(m_state, 1);
    top->addWidget(button);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_connections, 1);
    layout->addWidget(m_rate);

    setStats({});
}

void ReceiverPanel::setStats(const ReceiverStats& stats) {
    QString state;
    QColor color;
    if (stats.listening) {
        state = tr("Listening on %1:%2").arg(stats.address).arg(stats.port);
        color = Theme::activeColor();
    } else if (!stats.error.isEmpty()) {
        state = stats.error;
        color = Theme::errorColor();
    } else {
        state = tr("Stopped");
        color = Theme::inactiveColor();
    }
    m_state->setText(QStringLiteral("<span style=\"color:%1\">%2</span> %3")
                         .arg(color.name(), QString(kDot), state.toHtmlEscaped()));
    showConnections(stats.connections);
    m_rate->setText(tr("%1 lines/s, %2 lines received")
                        .arg(Format::count(stats.linesPerSecond), Format::count(stats.totalLines)));
}

void ReceiverPanel::showConnections(const QVector<ConnectionInfo>& connections) {
    m_connections->setRowCount(static_cast<int>(connections.size()));
    for (int row = 0; row < connections.size(); ++row) {
        const ConnectionInfo& connection = connections[row];
        setCell(m_connections, row, AddressColumn, connection.address);
        setCell(m_connections, row, ConnectedColumn,
                QDateTime::fromMSecsSinceEpoch(connection.connectedAt).toString(QStringLiteral("HH:mm:ss")));
        setCell(m_connections, row, LinesColumn, Format::count(connection.lines));
    }
}
