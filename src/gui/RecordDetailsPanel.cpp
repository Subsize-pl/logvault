#include "RecordDetailsPanel.h"

#include "Format.h"
#include "LineParser.h"
#include "SearchQuery.h"
#include "Theme.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

enum Column { NameColumn, ValueColumn, ColumnCount };

constexpr int kKindRole = Qt::UserRole;
constexpr int kValueRole = Qt::UserRole + 1;
constexpr int kFilterableRole = Qt::UserRole + 2;

// A search for a message uses its first line, at most this long: the search matches parts of messages, so a
// prefix finds the same records.
constexpr qsizetype kMaxSearchedLength = 200;

} // namespace

RecordDetailsPanel::RecordDetailsPanel(QWidget* parent)
    : QWidget(parent), m_fields(new QTreeWidget), m_raw(new QPlainTextEdit) {
    m_fields->setColumnCount(ColumnCount);
    m_fields->setHeaderLabels({tr("Field"), tr("Value")});
    m_fields->setRootIsDecorated(false);
    m_fields->setUniformRowHeights(true);
    m_fields->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fields->header()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    connect(m_fields, &QTreeWidget::customContextMenuRequested, this, &RecordDetailsPanel::showContextMenu);
    connect(m_fields, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item) { filterBy(item, false); });

    m_raw->setReadOnly(true);
    m_raw->setFont(Theme::monospaceFont());

    auto* rawBox = new QWidget;
    auto* rawLayout = new QVBoxLayout(rawBox);
    rawLayout->setContentsMargins(0, 0, 0, 0);
    rawLayout->addWidget(new QLabel(tr("Raw line")));
    rawLayout->addWidget(m_raw);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_fields);
    splitter->addWidget(rawBox);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
}

void RecordDetailsPanel::showRecord(const Record& record) {
    clear();
    m_record = record;
    addItem(Kind::Time, tr("Time"), Format::timestamp(record.ts), false);
    addItem(Kind::Level, tr("Level"), levelName(record.level), true);
    addItem(Kind::Source, tr("Source"), record.source, true);
    addItem(Kind::Message, tr("Message"), record.message, !Format::firstLine(record.message).isEmpty());

    const QJsonObject fields = QJsonDocument::fromJson(record.fields.toUtf8()).object();
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        // A null has no text the search could match, so it can only be copied.
        const std::optional<QString> text = LineParser::scalarText(it.value());
        addItem(Kind::Field, it.key(), text.value_or(u"null"_s),
                text.has_value() && SearchQuery::isValidFieldName(it.key()));
    }
    m_raw->setPlainText(record.raw);
}

void RecordDetailsPanel::clear() {
    m_fields->clear();
    m_raw->clear();
    m_record = {};
}

void RecordDetailsPanel::addItem(Kind kind, const QString& name, const QString& value, bool filterable) {
    auto* item = new QTreeWidgetItem(m_fields, {name, Format::firstLine(value)});
    item->setData(NameColumn, kKindRole, static_cast<int>(kind));
    item->setData(NameColumn, kValueRole, value);
    item->setData(NameColumn, kFilterableRole, filterable);
    if (kind == Kind::Level) {
        item->setForeground(ValueColumn, Theme::levelColor(m_record.level));
    }
}

void RecordDetailsPanel::showContextMenu(const QPoint& position) {
    const QTreeWidgetItem* item = m_fields->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const bool filterable = item->data(NameColumn, kFilterableRole).toBool();
    QMenu menu(this);
    QAction* search = menu.addAction(tr("Search for This Value"));
    QAction* exclude = menu.addAction(tr("Exclude This Value"));
    menu.addSeparator();
    QAction* copy = menu.addAction(tr("Copy"));
    search->setEnabled(filterable);
    exclude->setEnabled(filterable);
    const QAction* chosen = menu.exec(m_fields->viewport()->mapToGlobal(position));
    if (chosen == search || chosen == exclude) {
        filterBy(item, chosen == exclude);
    } else if (chosen == copy) {
        QGuiApplication::clipboard()->setText(item->data(NameColumn, kValueRole).toString());
    }
}

void RecordDetailsPanel::filterBy(const QTreeWidgetItem* item, bool exclude) {
    if (item == nullptr || !item->data(NameColumn, kFilterableRole).toBool()) {
        return;
    }
    const QString prefix = exclude ? u"-"_s : QString();
    switch (static_cast<Kind>(item->data(NameColumn, kKindRole).toInt())) {
    case Kind::Time:
        break;
    case Kind::Level:
        emit levelFilterRequested(m_record.level, exclude);
        break;
    case Kind::Source:
        emit sourceFilterRequested(m_record.source, exclude);
        break;
    case Kind::Message:
        emit searchTermRequested(prefix +
                                 SearchQuery::quote(Format::firstLine(m_record.message, kMaxSearchedLength)));
        break;
    case Kind::Field:
        // A quoted value matches exactly this value, whatever characters it has (DESIGN.md section 6.2).
        emit searchTermRequested(prefix + item->text(NameColumn) + u':' +
                                 SearchQuery::quote(item->data(NameColumn, kValueRole).toString()));
        break;
    }
}
