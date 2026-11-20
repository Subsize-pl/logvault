#include "DeleteRecordsDialog.h"

#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

// The cut-off the dialog suggests: the start of the day a month ago.
constexpr int kDefaultAgeDays = 30;

} // namespace

DeleteRecordsDialog::DeleteRecordsDialog(const QVector<SourceInfo>& sources,
                                         std::optional<qint64> selectedSourceId, QWidget* parent)
    : QDialog(parent),
      m_before(new QDateTimeEdit(QDate::currentDate().addDays(-kDefaultAgeDays).startOfDay())),
      m_allSources(new QRadioButton(tr("All sources"))), m_oneSource(new QRadioButton(tr("Only:"))),
      m_source(new QComboBox) {
    setWindowTitle(tr("Delete Records Older Than"));
    m_before->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_before->setCalendarPopup(true);

    for (const SourceInfo& source : sources) {
        m_source->addItem(source.name, source.id);
    }
    const int selected = selectedSourceId ? m_source->findData(*selectedSourceId) : -1;
    if (selected >= 0) {
        m_source->setCurrentIndex(selected);
        m_oneSource->setChecked(true);
    } else {
        m_allSources->setChecked(true);
    }
    m_oneSource->setEnabled(!sources.isEmpty());
    m_source->setEnabled(m_oneSource->isChecked());
    connect(m_oneSource, &QRadioButton::toggled, m_source, &QWidget::setEnabled);

    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_oneSource);
    sourceRow->addWidget(m_source, 1);
    auto* form = new QFormLayout;
    form->addRow(tr("Delete records older than:"), m_before);
    form->addRow(tr("From:"), m_allSources);
    form->addRow(QString(), sourceRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("Delete..."), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

qint64 DeleteRecordsDialog::before() const { return m_before->dateTime().toMSecsSinceEpoch(); }

qint64 DeleteRecordsDialog::sourceId() const {
    return m_oneSource->isChecked() ? m_source->currentData().toLongLong() : 0;
}

QString DeleteRecordsDialog::sourceName() const {
    return m_oneSource->isChecked() ? m_source->currentText() : QString();
}
