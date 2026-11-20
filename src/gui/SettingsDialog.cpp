#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// The step of the arrows of the row limit.
constexpr int kRowStep = 1000;

// Selects the item whose data is the value.
template <typename Enum> void selectData(QComboBox* box, Enum value) {
    box->setCurrentIndex(box->findData(static_cast<int>(value)));
}

} // namespace

SettingsDialog::SettingsDialog(const Settings& settings, QWidget* parent)
    : QDialog(parent), m_port(new QSpinBox), m_thisComputer(new QRadioButton(tr("This computer only"))),
      m_wholeNetwork(new QRadioButton(tr("The whole network"))), m_defaultSource(new QLineEdit),
      m_autostart(new QCheckBox(tr("Start the receiver when a database is opened"))),
      m_timeZone(new QComboBox), m_theme(new QComboBox), m_maxRows(new QSpinBox) {
    setWindowTitle(tr("Settings"));

    m_port->setRange(1, 65535);
    m_port->setToolTip(tr("The TCP port the receiver listens on. The Python client sends to 5170."));
    m_thisComputer->setToolTip(tr("Only programs on this computer can send logs (127.0.0.1)."));
    m_wholeNetwork->setToolTip(
        tr("Programs on other computers can send logs too (0.0.0.0). Windows may ask to allow this."));
    m_defaultSource->setPlaceholderText(Settings().defaultSource);
    m_defaultSource->setToolTip(tr("The source of the records received over TCP that do not name their own "
                                   "(a JSON field service, source or app)."));
    auto* address = new QVBoxLayout;
    address->addWidget(m_thisComputer);
    address->addWidget(m_wholeNetwork);
    auto* receiver = new QGroupBox(tr("Receiver"));
    auto* receiverForm = new QFormLayout(receiver);
    receiverForm->addRow(tr("Port:"), m_port);
    receiverForm->addRow(tr("Listen on:"), address);
    receiverForm->addRow(tr("Default source:"), m_defaultSource);
    receiverForm->addRow(QString(), m_autostart);

    m_timeZone->addItem(tr("Local time"), static_cast<int>(Settings::TimeZoneMode::Local));
    m_timeZone->addItem(tr("UTC"), static_cast<int>(Settings::TimeZoneMode::Utc));
    m_timeZone->setToolTip(tr("The zone of every time LogVault shows or asks for."));
    m_theme->addItem(tr("System"), static_cast<int>(Settings::ThemeMode::System));
    m_theme->addItem(tr("Light"), static_cast<int>(Settings::ThemeMode::Light));
    m_theme->addItem(tr("Dark"), static_cast<int>(Settings::ThemeMode::Dark));
    m_maxRows->setRange(Settings::kMinRows, Settings::kMaxRows);
    m_maxRows->setSingleStep(kRowStep);
    m_maxRows->setGroupSeparatorShown(true);
    // Numbers are written the English way everywhere in the UI.
    m_maxRows->setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    m_maxRows->setToolTip(tr("The table shows the newest matches, at most this many. "
                             "The export takes every match."));
    auto* display = new QGroupBox(tr("Display"));
    auto* displayForm = new QFormLayout(display);
    displayForm->addRow(tr("Time zone:"), m_timeZone);
    displayForm->addRow(tr("Theme:"), m_theme);
    displayForm->addRow(tr("Rows in the table:"), m_maxRows);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::RestoreDefaults);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { showSettings(Settings()); });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(receiver);
    layout->addWidget(display);
    layout->addWidget(buttons);
    showSettings(settings);
}

Settings SettingsDialog::settings() const {
    Settings settings;
    settings.port = static_cast<quint16>(m_port->value());
    settings.wholeNetwork = m_wholeNetwork->isChecked();
    if (const QString source = m_defaultSource->text().trimmed(); !source.isEmpty()) {
        settings.defaultSource = source;
    }
    settings.autostartReceiver = m_autostart->isChecked();
    settings.timeZone = static_cast<Settings::TimeZoneMode>(m_timeZone->currentData().toInt());
    settings.theme = static_cast<Settings::ThemeMode>(m_theme->currentData().toInt());
    settings.maxRows = m_maxRows->value();
    return settings;
}

void SettingsDialog::focusReceiver() {
    m_port->setFocus();
    m_port->selectAll();
}

void SettingsDialog::showSettings(const Settings& settings) {
    m_port->setValue(settings.port);
    m_wholeNetwork->setChecked(settings.wholeNetwork);
    m_thisComputer->setChecked(!settings.wholeNetwork);
    m_defaultSource->setText(settings.defaultSource);
    m_autostart->setChecked(settings.autostartReceiver);
    selectData(m_timeZone, settings.timeZone);
    selectData(m_theme, settings.theme);
    m_maxRows->setValue(settings.maxRows);
}
