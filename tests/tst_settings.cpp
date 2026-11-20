#include "tst_settings.h"

#include "Settings.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// A settings file of its own in a temporary folder: the tests never touch the user's settings.
class SettingsFile {
public:
    SettingsFile() : m_store(m_dir.filePath(u"settings.ini"_s), QSettings::IniFormat) {}

    QSettings& store() { return m_store; }
    bool isValid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QSettings m_store;
};

} // namespace

void TestSettings::emptyFileGivesDefaults() {
    SettingsFile file;
    QVERIFY(file.isValid());
    const Settings settings = Settings::load(file.store());
    QVERIFY(settings == Settings());
    QCOMPARE(settings.port, quint16(5170));
    QCOMPARE(settings.defaultSource, u"tcp"_s);
    QCOMPARE(settings.maxRows, 10'000);
    QVERIFY(!settings.wholeNetwork);
    QVERIFY(!settings.autostartReceiver);
    QVERIFY(settings.timeZone == Settings::TimeZoneMode::Local);
    QVERIFY(settings.theme == Settings::ThemeMode::System);
}

void TestSettings::savedSettingsReadBack() {
    SettingsFile file;
    Settings settings;
    settings.port = 6000;
    settings.wholeNetwork = true;
    settings.autostartReceiver = true;
    settings.defaultSource = u"shop \u0442\u0435\u0441\u0442"_s;
    settings.timeZone = Settings::TimeZoneMode::Utc;
    settings.theme = Settings::ThemeMode::Dark;
    settings.maxRows = 2500;
    settings.save(file.store());
    file.store().sync();
    QCOMPARE(file.store().status(), QSettings::NoError);

    // Read back through a second object, as the next launch would.
    QSettings again(file.store().fileName(), QSettings::IniFormat);
    QVERIFY(Settings::load(again) == settings);
    // Choices are words in the file.
    QCOMPARE(again.value(u"display/theme"_s).toString(), u"dark"_s);
    QCOMPARE(again.value(u"display/timeZone"_s).toString(), u"utc"_s);
}

void TestSettings::damagedValuesReadAsDefaults() {
    SettingsFile file;
    QSettings& store = file.store();
    const Settings defaults;
    for (const QString& port : {u"abc"_s, u"0"_s, u"-1"_s, u"65536"_s, QString()}) {
        store.setValue(u"receiver/port"_s, port);
        QCOMPARE(Settings::load(store).port, defaults.port);
    }
    store.setValue(u"receiver/port"_s, u"65535"_s);
    QCOMPARE(Settings::load(store).port, quint16(65535));

    store.setValue(u"receiver/defaultSource"_s, u"   "_s);
    QCOMPARE(Settings::load(store).defaultSource, u"tcp"_s);
    store.setValue(u"receiver/defaultSource"_s, u"  web  "_s);
    QCOMPARE(Settings::load(store).defaultSource, u"web"_s);

    store.setValue(u"display/theme"_s, u"purple"_s);
    store.setValue(u"display/timeZone"_s, u"Mars"_s);
    QVERIFY(Settings::load(store).theme == Settings::ThemeMode::System);
    QVERIFY(Settings::load(store).timeZone == Settings::TimeZoneMode::Local);
    // Words in any case.
    store.setValue(u"display/theme"_s, u"Light"_s);
    store.setValue(u"display/timeZone"_s, u"UTC"_s);
    QVERIFY(Settings::load(store).theme == Settings::ThemeMode::Light);
    QVERIFY(Settings::load(store).timeZone == Settings::TimeZoneMode::Utc);

    // A number out of the range is brought into it; anything else is the default.
    store.setValue(u"display/maxRows"_s, u"many"_s);
    QCOMPARE(Settings::load(store).maxRows, defaults.maxRows);
    store.setValue(u"display/maxRows"_s, 5);
    QCOMPARE(Settings::load(store).maxRows, Settings::kMinRows);
    store.setValue(u"display/maxRows"_s, 10'000'000);
    QCOMPARE(Settings::load(store).maxRows, Settings::kMaxRows);
}

void TestSettings::derivedValuesFollowTheChoices() {
    Settings settings;
    QCOMPARE(settings.receiverAddress().toString(), u"127.0.0.1"_s);
    QCOMPARE(settings.displayZone().timeSpec(), Qt::LocalTime);
    QCOMPARE(settings.colorScheme(), Qt::ColorScheme::Unknown);

    settings.wholeNetwork = true;
    settings.timeZone = Settings::TimeZoneMode::Utc;
    QCOMPARE(settings.receiverAddress().toString(), u"0.0.0.0"_s);
    QCOMPARE(settings.displayZone().timeSpec(), Qt::UTC);

    settings.theme = Settings::ThemeMode::Light;
    QCOMPARE(settings.colorScheme(), Qt::ColorScheme::Light);
    settings.theme = Settings::ThemeMode::Dark;
    QCOMPARE(settings.colorScheme(), Qt::ColorScheme::Dark);
}

void TestSettings::recentListKeepsTheNewestFirst() {
    QStringList recent;
    for (int i = 1; i <= 7; ++i) {
        recent = RecentDatabases::added(recent, u"C:/logs/%1.lvdb"_s.arg(i));
    }
    QCOMPARE(recent, (QStringList{u"C:/logs/7.lvdb"_s, u"C:/logs/6.lvdb"_s, u"C:/logs/5.lvdb"_s,
                                  u"C:/logs/4.lvdb"_s, u"C:/logs/3.lvdb"_s}));

    // The same file, written another way, moves to the front instead of being listed twice.
    recent = RecentDatabases::added(recent, u"c:\\LOGS\\sub\\..\\5.LVDB"_s);
    QCOMPARE(recent.size(), qsizetype(RecentDatabases::kMaxCount));
    QCOMPARE(recent.front().toLower(), u"c:/logs/5.lvdb"_s);
    QCOMPARE(recent.count(u"C:/logs/5.lvdb"_s), qsizetype(0));
    QCOMPARE(recent.at(1), u"C:/logs/7.lvdb"_s);
}

void TestSettings::recentListRemovesOnlyThatFile() {
    // None of these files exists, which must not make them all "the same file".
    const QStringList recent = {u"C:/missing/a.lvdb"_s, u"C:/missing/b.lvdb"_s, u"C:/missing/c.lvdb"_s};
    QCOMPARE(RecentDatabases::removed(recent, u"C:\\Missing\\B.lvdb"_s),
             (QStringList{u"C:/missing/a.lvdb"_s, u"C:/missing/c.lvdb"_s}));
    QCOMPARE(RecentDatabases::removed(recent, u"C:/missing/d.lvdb"_s), recent);
}

void TestSettings::recentListIsCleanedUpWhenRead() {
    SettingsFile file;
    file.store().setValue(u"recentDatabases"_s,
                          QStringList{u"C:/a.lvdb"_s, QString(), u"C:/b.lvdb"_s, u"c:/A.lvdb"_s, u"  "_s,
                                      u"C:/c.lvdb"_s, u"C:/d.lvdb"_s, u"C:/e.lvdb"_s, u"C:/f.lvdb"_s});
    QCOMPARE(RecentDatabases::load(file.store()),
             (QStringList{u"C:/a.lvdb"_s, u"C:/b.lvdb"_s, u"C:/c.lvdb"_s, u"C:/d.lvdb"_s, u"C:/e.lvdb"_s}));

    RecentDatabases::save(file.store(), {u"C:/x.lvdb"_s});
    QCOMPARE(RecentDatabases::load(file.store()), QStringList{u"C:/x.lvdb"_s});
    QVERIFY(RecentDatabases::load(SettingsFile().store()).isEmpty());
}
