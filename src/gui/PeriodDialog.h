#pragma once

#include <QDialog>
#include <QTimeZone>

class QDateTimeEdit;
class QDialogButtonBox;
class QLabel;

/// Asks for a custom period (DESIGN.md section 2.2): its start and end as date and time in the display zone.
class PeriodDialog : public QDialog {
    Q_OBJECT

public:
    /// The fields start at from and to, ms UTC, shown in the zone.
    PeriodDialog(qint64 from, qint64 to, const QTimeZone& zone, QWidget* parent = nullptr);

    /// The chosen period in ms UTC; the end is exclusive.
    qint64 from() const;
    qint64 to() const;

private:
    void validate();

    QDateTimeEdit* m_from;
    QDateTimeEdit* m_to;
    QLabel* m_problem;
    QDialogButtonBox* m_buttons;
};
