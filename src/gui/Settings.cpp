#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The groups of the keys follow the parts of the settings dialog.
constexpr auto kPortKey = "receiver/port"_L1;
constexpr auto kWholeNetworkKey = "receiver/wholeNetwork"_L1;
constexpr auto kAutostartKey = "receiver/autostart"_L1;
constexpr auto kDefaultSourceKey = "receiver/defaultSource"_L1;
constexpr auto kTimeZoneKey = "display/timeZone"_L1;
constexpr auto kThemeKey = "display/theme"_L1;
constexpr auto kMaxRowsKey = "display/maxRows"_L1;
constexpr auto kRecentKey = "recentDatabases"_L1;

// Choices are stored as words, so the settings file can be read and edited by hand.
template <typename Enum, std::size_t Count>
using Names = std::array<std::pair<Enum, QLatin1StringView>, Count>;

constexpr Names<Settings::TimeZoneMode, 2> kTimeZoneNames = {
    {{Settings::TimeZoneMode::Local, "local"_L1}, {Settings::TimeZoneMode::Utc, "utc"_L1}}};
constexpr Names<Settings::ThemeMode, 3> kThemeNames = {{{Settings::ThemeMode::System, "system"_L1},
                                                        {Settings::ThemeMode::Light, "light"_L1},
                                                        {Settings::ThemeMode::Dark, "dark"_L1}}};

// The choice named by the text, in any case; the fallback for any other text.
template <typename Enum, std::size_t Count>
Enum fromName(const QString& text, const Names<Enum, Count>& names, Enum fallback) {
    const auto found = std::find_if(names.begin(), names.end(), [&text](const auto& entry) {
        return text.compare(entry.second, Qt::CaseInsensitive) == 0;
    });
    return found != names.end() ? found->first : fallback;
}

template <typename Enum, std::size_t Count> QString nameOf(Enum value, const Names<Enum, Count>& names) {
    const auto found =
        std::find_if(names.begin(), names.end(), [value](const auto& entry) { return entry.first == value; });
    return found != names.end() ? QString(found->second) : QString(names.front().second);
}

QString normalized(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }

// Windows file names ignore case, so C:/Logs/a.lvdb and c:/logs/A.lvdb are the same database.
bool samePath(const QString& first, const QString& second) {
    return normalized(first).compare(normalized(second), Qt::CaseInsensitive) == 0;
}

} // namespace

QHostAddress Settings::receiverAddress() const {
    return QHostAddress(wholeNetwork ? QHostAddress::AnyIPv4 : QHostAddress::LocalHost);
}

QTimeZone Settings::displayZone() const {
    return QTimeZone(timeZone == TimeZoneMode::Utc ? QTimeZone::UTC : QTimeZone::LocalTime);
}

Qt::ColorScheme Settings::colorScheme() const {
    switch (theme) {
    case ThemeMode::Light:
        return Qt::ColorScheme::Light;
    case ThemeMode::Dark:
        return Qt::ColorScheme::Dark;
    case ThemeMode::System:
        break;
    }
    return Qt::ColorScheme::Unknown;
}

Settings Settings::load(const QSettings& store) {
    Settings settings;
    bool ok = false;
    const int port = store.value(kPortKey).toInt(&ok);
    if (ok && port >= 1 && port <= 65535) {
        settings.port = static_cast<quint16>(port);
    }
    settings.wholeNetwork = store.value(kWholeNetworkKey, settings.wholeNetwork).toBool();
    settings.autostartReceiver = store.value(kAutostartKey, settings.autostartReceiver).toBool();
    // A blank name would leave TCP records without a source; the server falls back to "tcp" as well.
    if (const QString source = store.value(kDefaultSourceKey).toString().trimmed(); !source.isEmpty()) {
        settings.defaultSource = source;
    }
    settings.timeZone = fromName(store.value(kTimeZoneKey).toString(), kTimeZoneNames, settings.timeZone);
    settings.theme = fromName(store.value(kThemeKey).toString(), kThemeNames, settings.theme);
    const int maxRows = store.value(kMaxRowsKey).toInt(&ok);
    if (ok) {
        settings.maxRows = qBound(kMinRows, maxRows, kMaxRows);
    }
    return settings;
}

void Settings::save(QSettings& store) const {
    store.setValue(kPortKey, port);
    store.setValue(kWholeNetworkKey, wholeNetwork);
    store.setValue(kAutostartKey, autostartReceiver);
    store.setValue(kDefaultSourceKey, defaultSource);
    store.setValue(kTimeZoneKey, nameOf(timeZone, kTimeZoneNames));
    store.setValue(kThemeKey, nameOf(theme, kThemeNames));
    store.setValue(kMaxRowsKey, maxRows);
}

namespace RecentDatabases {

QStringList load(const QSettings& store) {
    // Cleaned up like any list the program makes, in case the file was edited by hand.
    QStringList paths;
    const QStringList stored = store.value(kRecentKey).toStringList();
    for (auto it = stored.crbegin(); it != stored.crend(); ++it) {
        if (!it->trimmed().isEmpty()) {
            paths = added(paths, *it);
        }
    }
    return paths;
}

void save(QSettings& store, const QStringList& paths) { store.setValue(kRecentKey, paths); }

QStringList added(const QStringList& paths, const QString& path) {
    QStringList result = removed(paths, path);
    result.prepend(normalized(path));
    if (result.size() > kMaxCount) {
        result.resize(kMaxCount);
    }
    return result;
}

QStringList removed(const QStringList& paths, const QString& path) {
    QStringList result;
    std::copy_if(paths.begin(), paths.end(), std::back_inserter(result),
                 [&path](const QString& entry) { return !samePath(entry, path); });
    return result;
}

} // namespace RecentDatabases
