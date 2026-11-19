#include "tst_storage.h"

#include "Record.h"

#include <QTest>

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
