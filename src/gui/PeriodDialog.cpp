#include "PeriodDialog.h"

#include "Theme.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QDateTimeEdit* createEdit(qint64 msecs) {
    // The fields show whole seconds, so hidden milliseconds must not move the bounds.
    auto* edit = new QDateTimeEdit(QDateTime::fromMSecsSinceEpoch(msecs / 1000 * 1000));
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    edit->setCalendarPopup(true);
    return edit;
}

} // namespace

PeriodDialog::PeriodDialog(qint64 from, qint64 to, QWidget* parent)
    : QDialog(parent), m_from(createEdit(from)), m_to(createEdit(to)),
      m_problem(new QLabel(tr("The start must be before the end."))),
      m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel)) {
    setWindowTitle(tr("Custom Range"));
    m_problem->setStyleSheet(QStringLiteral("color: %1").arg(Theme::errorColor().name()));

    auto* form = new QFormLayout;
    form->addRow(tr("From:"), m_from);
    form->addRow(tr("To:"), m_to);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_problem);
    layout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this, &PeriodDialog::validate);
    connect(m_to, &QDateTimeEdit::dateTimeChanged, this, &PeriodDialog::validate);
    validate();
}

qint64 PeriodDialog::from() const { return m_from->dateTime().toMSecsSinceEpoch(); }

qint64 PeriodDialog::to() const { return m_to->dateTime().toMSecsSinceEpoch(); }

void PeriodDialog::validate() {
    const bool valid = from() < to();
    m_problem->setVisible(!valid);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
}
