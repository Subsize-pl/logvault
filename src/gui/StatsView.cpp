#include "StatsView.h"

#include "Format.h"
#include "LogStore.h"
#include "Theme.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QStringList>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

enum Column { ValueColumn, CountColumn, ShareColumn, ColumnCount };

bool sameGroups(const QVector<StatsRow>& a, const QVector<StatsRow>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const StatsRow& x, const StatsRow& y) {
        return x.value == y.value && x.id == y.id && x.noField == y.noField;
    });
}

QStandardItem* numberItem() {
    auto* item = new QStandardItem;
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace

StatsView::StatsView(QWidget* parent)
    : QWidget(parent), m_groupBy(new QComboBox), m_field(new QLineEdit), m_summary(new QLabel),
      m_table(new QTableView), m_model(new QStandardItemModel(0, ColumnCount, this)) {
    m_groupBy->addItem(tr("Level"), static_cast<int>(StatsMode::Level));
    m_groupBy->addItem(tr("Source"), static_cast<int>(StatsMode::Source));
    m_groupBy->addItem(tr("Similar messages"), static_cast<int>(StatsMode::Message));
    m_groupBy->addItem(tr("Field..."), static_cast<int>(StatsMode::Field));
    m_field->setPlaceholderText(tr("Field name, e.g. provider"));
    m_field->setClearButtonEnabled(true);
    m_field->hide();
    m_summary->setToolTip(tr("Double-click a group to filter the search by it."));

    m_table->setModel(m_model);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->hide();
    QHeaderView* header = m_table->horizontalHeader();
    header->setSectionResizeMode(ValueColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(CountColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ShareColumn, QHeaderView::ResizeToContents);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Group by:")));
    controls->addWidget(m_groupBy);
    controls->addWidget(m_field);
    controls->addStretch();
    controls->addWidget(m_summary);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(m_table, 1);

    connect(m_groupBy, &QComboBox::activated, this, &StatsView::groupingSelected);
    connect(m_field, &QLineEdit::returnPressed, this, &StatsView::groupingSelected);
    // A double-click or Enter on a group.
    connect(m_table, &QAbstractItemView::activated, this, &StatsView::rowActivated);
    showRows({}, 0);
}

void StatsView::prepareRequest(SearchRequest& request, bool shown) {
    m_requestedMode = shown ? chosenMode() : StatsMode::None;
    m_requestedField = m_requestedMode == StatsMode::Field ? m_field->text().trimmed() : QString();
    request.statsMode = m_requestedMode;
    request.statsField = m_requestedField;
}

void StatsView::showResult(const SearchResult& result) {
    // The items are rebuilt only when the grouping changed; see showRows().
    const bool sameGrouping = m_rowsMode == m_requestedMode && m_rowsField == m_requestedField;
    m_rowsMode = m_requestedMode;
    m_rowsField = m_requestedField;
    if (!sameGrouping) {
        m_rows.clear();
    }
    showRows(m_rowsMode == StatsMode::None ? QVector<StatsRow>() : result.stats, result.total);
}

void StatsView::clear() {
    m_requestedMode = StatsMode::None;
    m_requestedField.clear();
    m_rowsMode = StatsMode::None;
    m_rowsField.clear();
    m_rows.clear();
    showRows({}, 0);
}

QString StatsView::messageSearchTerm(const QString& pattern) {
    // Digits became '#', so the parts between them are literally in every message of the group.
    QString longest;
    for (const QString& part : Format::firstLine(pattern).split(u'#')) {
        const QString trimmed = part.trimmed();
        if (trimmed.size() > longest.size()) {
            longest = trimmed;
        }
    }
    return longest.isEmpty() ? QString() : SearchQuery::quote(longest);
}

QString StatsView::shareText(qint64 count, qint64 total) {
    if (total <= 0) {
        return {};
    }
    const double share = 100.0 * static_cast<double>(count) / static_cast<double>(total);
    if (count > 0 && share < 0.05) {
        return u"< 0.1 %"_s;
    }
    return QString::number(share, 'f', 1) + u" %"_s;
}

StatsMode StatsView::chosenMode() const {
    const auto mode = static_cast<StatsMode>(m_groupBy->currentData().toInt());
    // Grouping by a field needs a valid name; until then the search computes no statistics, and fails on
    // none.
    if (mode == StatsMode::Field && !SearchQuery::isValidFieldName(m_field->text().trimmed())) {
        return StatsMode::None;
    }
    return mode;
}

void StatsView::groupingSelected() {
    const bool byField = static_cast<StatsMode>(m_groupBy->currentData().toInt()) == StatsMode::Field;
    m_field->setVisible(byField);
    if (byField && m_field->text().trimmed().isEmpty()) {
        m_field->setFocus();
    }
    updateSummary();
    emit groupingChanged();
}

void StatsView::showRows(const QVector<StatsRow>& rows, qint64 total) {
    // Live mode refreshes every second. While the groups stay the same only the numbers change, so the
    // selection and the scroll position stay where the user left them.
    const bool sameItems = sameGroups(rows, m_rows) && m_model->rowCount() == rows.size();
    m_rows = rows;
    m_total = total;
    if (!sameItems) {
        m_model->removeRows(0, m_model->rowCount());
        const QColor base = m_table->palette().color(QPalette::Base);
        for (const StatsRow& row : m_rows) {
            auto* value = new QStandardItem(Format::firstLine(row.value));
            value->setToolTip(Format::firstLine(row.value));
            QFont font = m_table->font();
            if (m_rowsMode == StatsMode::Level) {
                value->setForeground(Theme::levelTextColor(levelFromValue(static_cast<int>(row.id)), base));
                font.setBold(true);
            } else if (m_rowsMode == StatsMode::Message) {
                font = Theme::monospaceFont();
            }
            if (row.noField) {
                // Not a value of the field, so it must not look like one.
                font.setItalic(true);
                value->setForeground(m_table->palette().color(QPalette::PlaceholderText));
            }
            value->setFont(font);
            m_model->appendRow({value, numberItem(), numberItem()});
        }
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        m_model->item(i, CountColumn)->setText(Format::count(m_rows[i].count));
        m_model->item(i, ShareColumn)->setText(shareText(m_rows[i].count, total));
    }
    m_model->setHorizontalHeaderLabels({valueHeader(), tr("Count"), tr("Share")});
    updateSummary();
}

void StatsView::updateSummary() {
    const auto chosen = static_cast<StatsMode>(m_groupBy->currentData().toInt());
    const QString name = m_field->text().trimmed();
    if (chosen == StatsMode::Field && name.isEmpty()) {
        m_summary->setText(tr("Type a field name and press Enter."));
    } else if (chosen == StatsMode::Field && !SearchQuery::isValidFieldName(name)) {
        m_summary->setText(tr("\"%1\" is not a field name: use letters, digits and _ . @ -").arg(name));
    } else if (m_rowsMode == StatsMode::None) {
        m_summary->clear();
    } else if (m_rows.isEmpty()) {
        m_summary->setText(tr("No matching records."));
    } else {
        const QString matches = m_total == 1 ? tr("1 match") : tr("%1 matches").arg(Format::count(m_total));
        const bool limited = (m_rowsMode == StatsMode::Message || m_rowsMode == StatsMode::Field) &&
                             m_rows.size() >= LogStore::kStatsRowLimit;
        if (limited) {
            m_summary->setText(tr("The %1 largest groups of %2").arg(Format::count(m_rows.size()), matches));
        } else if (m_rows.size() == 1) {
            m_summary->setText(tr("1 group of %1").arg(matches));
        } else {
            m_summary->setText(tr("%1 groups of %2").arg(Format::count(m_rows.size()), matches));
        }
    }
}

void StatsView::rowActivated(const QModelIndex& index) {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return;
    }
    const StatsRow& row = m_rows.at(index.row());
    switch (m_rowsMode) {
    case StatsMode::Level:
        emit levelRequested(levelFromValue(static_cast<int>(row.id)));
        break;
    case StatsMode::Source:
        emit sourceRequested(row.id);
        break;
    case StatsMode::Message:
        if (const QString term = messageSearchTerm(row.value); !term.isEmpty()) {
            emit searchTermRequested(term);
        }
        break;
    case StatsMode::Field:
        // The "(no field)" group: the records without the field (DESIGN.md section 6.5).
        emit searchTermRequested(row.noField ? u'-' + m_rowsField + u":*"_s
                                             : m_rowsField + u':' + SearchQuery::quote(row.value));
        break;
    case StatsMode::None:
        break;
    }
}

QString StatsView::valueHeader() const {
    switch (m_rowsMode) {
    case StatsMode::Level:
        return tr("Level");
    case StatsMode::Source:
        return tr("Source");
    case StatsMode::Message:
        return tr("Message pattern");
    case StatsMode::Field:
        return m_rowsField;
    case StatsMode::None:
        break;
    }
    return tr("Group");
}
