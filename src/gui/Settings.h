#pragma once

#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <Qt>

class QSettings;

/// The user's settings (DESIGN.md section 1.2, F16) and how they are kept in QSettings. A value that is
/// missing, damaged or out of range reads as its default, so a broken settings file never breaks LogVault.
struct Settings {
    enum class TimeZoneMode { Local, Utc };
    enum class ThemeMode { System, Light, Dark };

    static constexpr quint16 kDefaultPort = 5170;
    static constexpr int kDefaultMaxRows = 10'000;
    /// The range of maxRows: a table of fewer rows shows too little, more would take too long to fill.
    static constexpr int kMinRows = 100;
    static constexpr int kMaxRows = 100'000;

    quint16 port = kDefaultPort;
    bool wholeNetwork = false; // listen on every network interface instead of on this computer only
    bool autostartReceiver = false;
    QString defaultSource = QStringLiteral("tcp"); // for TCP lines that name no source of their own
    TimeZoneMode timeZone = TimeZoneMode::Local;
    ThemeMode theme = ThemeMode::System;
    int maxRows = kDefaultMaxRows;

    /// 127.0.0.1, or 0.0.0.0 for the whole network.
    QHostAddress receiverAddress() const;
    /// The zone times are shown and entered in.
    QTimeZone displayZone() const;
    /// The color scheme to ask Qt for; Unknown follows the system.
    Qt::ColorScheme colorScheme() const;

    static Settings load(const QSettings& store);
    void save(QSettings& store) const;

    bool operator==(const Settings& other) const = default;
};

/// The recently opened databases (DESIGN.md section 1.2, F1): newest first, one entry per file, at most
/// kMaxCount. Kept with the settings.
namespace RecentDatabases {

inline constexpr int kMaxCount = 5;

QStringList load(const QSettings& store);
void save(QSettings& store, const QStringList& paths);

/// The list with the path at the front, moved there if it was in the list already.
QStringList added(const QStringList& paths, const QString& path);
/// The list without the path.
QStringList removed(const QStringList& paths, const QString& path);

} // namespace RecentDatabases
