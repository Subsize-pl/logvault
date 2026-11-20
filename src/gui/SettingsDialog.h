#pragma once

#include "Settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

/// The Settings dialog (DESIGN.md section 1.2, F16): the receiver's port, address, default source and
/// autostart; the display time zone, the theme and the rows in the table. It edits a copy of the settings,
/// which the caller applies.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings& settings, QWidget* parent = nullptr);

    /// The settings as edited; a blank default source becomes the default one.
    Settings settings() const;

    /// Puts the focus on the receiver's port, for Receiver > Receiver Settings.
    void focusReceiver();

private:
    void showSettings(const Settings& settings);

    QSpinBox* m_port;
    QRadioButton* m_thisComputer;
    QRadioButton* m_wholeNetwork;
    QLineEdit* m_defaultSource;
    QCheckBox* m_autostart;
    QComboBox* m_timeZone;
    QComboBox* m_theme;
    QSpinBox* m_maxRows;
};
