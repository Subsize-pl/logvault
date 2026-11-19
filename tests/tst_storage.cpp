#include "tst_storage.h"

#include "ConnectionGuard.h"
#include "Database.h"
#include "Record.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Database> && !std::is_copy_assignable_v<Database>,
              "a Database owns its connection and must not be copied");

namespace {

// Reads the journal mode of a file through a separate plain connection.
QString journalModeOf(const QString& path) {
    const QString connection = QStringLiteral("tst_storage_journal");
    const ConnectionGuard guard(connection);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(path);
    if (!db.open()) {
        return db.lastError().text();
    }
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode")) || !query.next()) {
        return query.lastError().text();
    }
    return query.value(0).toString();
}

} // namespace

void TestStorage::init() {
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_dir->isValid(), qPrintable(m_dir->errorString()));
}

void TestStorage::cleanup() { m_dir.reset(); }

QString TestStorage::databasePath(const QString& fileName) const { return m_dir->filePath(fileName); }

void TestStorage::levelNamesAndValues() {
    QCOMPARE(levelName(Level::Unknown), QStringLiteral("UNKNOWN"));
    QCOMPARE(levelName(Level::Trace), QStringLiteral("TRACE"));
    QCOMPARE(levelName(Level::Debug), QStringLiteral("DEBUG"));
    QCOMPARE(levelName(Level::Info), QStringLiteral("INFO"));
    QCOMPARE(levelName(Level::Warn), QStringLiteral("WARN"));
    QCOMPARE(levelName(Level::Error), QStringLiteral("ERROR"));
    QCOMPARE(levelName(Level::Fatal), QStringLiteral("FATAL"));

    // The stored values are part of the file format.
    QCOMPARE(static_cast<int>(Level::Unknown), 0);
    QCOMPARE(static_cast<int>(Level::Fatal), 6);
    QCOMPARE(levelFromValue(4), Level::Warn);
    QCOMPARE(levelFromValue(-1), Level::Unknown);
    QCOMPARE(levelFromValue(7), Level::Unknown);
}

void TestStorage::messageKeyGroupsDigits_data() {
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("key");

    QTest::newRow("design example") << QStringLiteral("Timeout after 3000 ms for order 8812")
                                    << QStringLiteral("Timeout after # ms for order #");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("no digits") << QStringLiteral("Connection reset") << QStringLiteral("Connection reset");
    QTest::newRow("only digits") << QStringLiteral("12345") << QStringLiteral("#");
    QTest::newRow("glued to letters") << QStringLiteral("a1b22c333") << QStringLiteral("a#b#c#");
    QTest::newRow("separators split runs") << QStringLiteral("10.0.0.1:8080") << QStringLiteral("#.#.#.#:#");
    QTest::newRow("long message is cut") << QString(300, u'x') << QString(200, u'x');
    QTest::newRow("cut after replacing") << QString(500, u'7') + QString(250, u'y')
                                         << u'#' + QString(199, u'y');
}

void TestStorage::messageKeyGroupsDigits() {
    QFETCH(QString, message);
    QFETCH(QString, key);
    QCOMPARE(messageKey(message), key);
}

void TestStorage::messageKeyKeepsSurrogatePairsWhole() {
    // U+1F600 is two UTF-16 code units; at positions 199-200 the cut would split it.
    const QString emoji = QStringLiteral(u"\U0001F600");
    QCOMPARE(emoji.size(), 2);
    const QString message = QString(199, u'x') + emoji + QStringLiteral("tail");
    QCOMPARE(messageKey(message), QString(199, u'x'));

    // When the pair fits completely, it is kept.
    const QString fits = QString(198, u'x') + emoji + QStringLiteral("tail");
    QCOMPARE(messageKey(fits), QString(198, u'x') + emoji);
}

void TestStorage::createsSchemaAndReopens() {
    const QString path = databasePath();
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
        QVERIFY(db.isOpen());
        QCOMPARE(db.path(), path);
        QVERIFY(!db.open(path)); // one object, one connection
    }
    QVERIFY(QFileInfo::exists(path));

    Database db;
    QVERIFY2(db.open(path), qPrintable(db.errorString()));
    QSqlQuery query(db.connection());
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString::number(Database::kSchemaVersion));
    QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("wal"));
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestStorage::rejectsNewerSchemaVersion() {
    const QString path = databasePath();
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
        QSqlQuery query(db.connection());
        QVERIFY(query.exec(QStringLiteral("UPDATE meta SET value = '2' WHERE key = 'schema_version'")));
    }

    Database db;
    QVERIFY(!db.open(path));
    QVERIFY(!db.isOpen());
    QVERIFY2(db.errorString().contains(QStringLiteral("newer version")), qPrintable(db.errorString()));
}

void TestStorage::rejectsForeignSqliteFile() {
    const QString path = databasePath(QStringLiteral("foreign.db"));
    {
        const QString connection = QStringLiteral("tst_storage_foreign");
        const ConnectionGuard guard(connection);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE notes (text TEXT)")));
    }

    Database db;
    QVERIFY(!db.open(path));
    QVERIFY2(db.errorString().contains(QStringLiteral("not a LogVault database")), qPrintable(db.errorString()));
    // The foreign file must stay untouched, including its journal mode.
    QCOMPARE(journalModeOf(path), QStringLiteral("delete"));
}

void TestStorage::rejectsNonSqliteFile() {
    const QString path = databasePath(QStringLiteral("notes.txt"));
    const QByteArray content = QByteArray("This is a plain text file, not a database.\n").repeated(40);
    {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(content), content.size());
    }

    Database db;
    QVERIFY(!db.open(path));
    QVERIFY2(db.errorString().contains(QStringLiteral("not a LogVault database")), qPrintable(db.errorString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), content);
}

void TestStorage::rejectsMissingDirectory() {
    Database db;
    QVERIFY(!db.open(databasePath(QStringLiteral("missing/sub/test.lvdb"))));
    QVERIFY(!db.errorString().isEmpty());
    QVERIFY(!db.open(QString()));
    QVERIFY(!db.errorString().isEmpty());
}

void TestStorage::opensPathWithSpacesAndNonAscii() {
    // "test dir" and "journal.lvdb" in Russian, written with escapes to keep the source ASCII.
    const QString dirName = QStringLiteral(u"тест dir");
    QVERIFY(QDir(m_dir->path()).mkdir(dirName));
    const QString path =
        QDir(m_dir->filePath(dirName)).filePath(QStringLiteral(u"журнал.lvdb"));
    {
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.errorString()));
    }
    QVERIFY(QFileInfo::exists(path));

    Database db;
    QVERIFY2(db.open(path), qPrintable(db.errorString()));
}
