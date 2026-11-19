#include "tst_linesplitter.h"

#include "LineSplitter.h"

#include <QTest>

namespace {

constexpr qsizetype kMax = LineSplitter::kMaxLineBytes;

// Feeds data in pieces of pieceSize bytes, then ends the stream; returns every line.
QVector<LineSplitter::Line> splitAll(QByteArrayView data, qsizetype pieceSize) {
    LineSplitter splitter;
    QVector<LineSplitter::Line> lines;
    for (qsizetype pos = 0; pos < data.size(); pos += pieceSize) {
        lines += splitter.append(data.sliced(pos, qMin(pieceSize, data.size() - pos)));
    }
    if (const std::optional<LineSplitter::Line> last = splitter.finish()) {
        lines.append(*last);
    }
    return lines;
}

QByteArrayList textsOf(const QVector<LineSplitter::Line>& lines) {
    QByteArrayList texts;
    for (const LineSplitter::Line& line : lines) {
        texts.append(line.text);
    }
    return texts;
}

} // namespace

void TestLineSplitter::splitsLinesAndDropsCarriageReturns() {
    LineSplitter splitter;
    const QVector<LineSplitter::Line> lines = splitter.append("first\nsecond\r\nthird\r\r\na\rb\n");
    // Only a '\r' right before '\n' belongs to the line break.
    QCOMPARE(textsOf(lines), (QByteArrayList{"first", "second", "third\r", "a\rb"}));
    for (const LineSplitter::Line& line : lines) {
        QVERIFY(!line.truncated);
    }
    QVERIFY(!splitter.finish());
}

void TestLineSplitter::keepsUnfinishedLineAcrossCalls() {
    LineSplitter splitter;
    QVERIFY(splitter.append(R"({"msg":"Hel)").isEmpty());
    QVERIFY(splitter.append("lo").isEmpty());
    QCOMPARE(textsOf(splitter.append("\"}\n{\"msg\"")), QByteArrayList{R"({"msg":"Hello"})"});
    const std::optional<LineSplitter::Line> last = splitter.finish();
    QVERIFY(last);
    QCOMPARE(last->text, QByteArray(R"({"msg")"));
    QVERIFY(!last->truncated);
}

void TestLineSplitter::sameLinesWhenFedByteByByte() {
    const QByteArray data = "alpha\r\nbeta\n\ngamma\r\n delta \nepsilon";
    const QByteArrayList whole = textsOf(splitAll(data, data.size()));
    QCOMPARE(whole, (QByteArrayList{"alpha", "beta", "gamma", " delta ", "epsilon"}));
    QCOMPARE(textsOf(splitAll(data, 1)), whole);
    QCOMPARE(textsOf(splitAll(data, 3)), whole);
}

void TestLineSplitter::skipsBlankLines() {
    QCOMPARE(textsOf(splitAll("\n\n   \n\t\r\n \t \nx\n\r\n  ", 1000)), QByteArrayList{"x"});
}

void TestLineSplitter::finishReturnsTheTail() {
    LineSplitter splitter;
    QVERIFY(!splitter.finish());
    QVERIFY(!splitter.append("done\n").isEmpty());
    QVERIFY(!splitter.finish());

    QVERIFY(splitter.append("tail\r").isEmpty());
    const std::optional<LineSplitter::Line> tail = splitter.finish();
    QVERIFY(tail);
    QCOMPARE(tail->text, QByteArray("tail"));

    // finish() starts a new stream.
    QVERIFY(!splitter.finish());
    QCOMPARE(textsOf(splitter.append("next\n")), QByteArrayList{"next"});
    QVERIFY(splitter.append("   ").isEmpty());
    QVERIFY(!splitter.finish());
}

void TestLineSplitter::cutsLongLines_data() {
    QTest::addColumn<int>("pieceSize");
    QTest::newRow("one piece") << 1'000'000;
    QTest::newRow("1000-byte pieces") << 1000;
    QTest::newRow("7-byte pieces") << 7;
}

void TestLineSplitter::cutsLongLines() {
    QFETCH(int, pieceSize);
    const QVector<LineSplitter::Line> lines = splitAll(QByteArray(100'000, 'x') + "\nnext\n", pieceSize);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines[0].text, QByteArray(kMax, 'x'));
    QVERIFY(lines[0].truncated);
    QCOMPARE(lines[1].text, QByteArray("next"));
    QVERIFY(!lines[1].truncated);
}

void TestLineSplitter::limitIsExact() {
    const QByteArray atLimit(kMax, 'a');
    // A line of exactly the limit stays whole with either line break, however it arrives.
    for (const QByteArray& lineBreak : {QByteArray("\n"), QByteArray("\r\n"), QByteArray()}) {
        for (const qsizetype pieceSize : {kMax + 2, kMax, qsizetype(4096), qsizetype(7)}) {
            const QVector<LineSplitter::Line> lines = splitAll(atLimit + lineBreak, pieceSize);
            QCOMPARE(lines.size(), 1);
            QCOMPARE(lines[0].text.size(), kMax);
            QVERIFY(!lines[0].truncated);
        }
    }
    // One byte more is cut.
    for (const QByteArray& lineBreak : {QByteArray("\n"), QByteArray("\r\n"), QByteArray()}) {
        for (const qsizetype pieceSize : {kMax + 3, qsizetype(4096)}) {
            const QVector<LineSplitter::Line> lines = splitAll(atLimit + 'b' + lineBreak, pieceSize);
            QCOMPARE(lines.size(), 1);
            QCOMPARE(lines[0].text, atLimit);
            QVERIFY(lines[0].truncated);
        }
    }
}

void TestLineSplitter::cutNeverSplitsUtf8Characters() {
    const QByteArray cyrillic = "\xD0\x96";      // two bytes
    const QByteArray emoji = "\xF0\x9F\x94\xA5"; // four bytes

    QVector<LineSplitter::Line> lines = splitAll(QByteArray(kMax - 1, 'x') + cyrillic + "tail\n", 1000);
    QCOMPARE(lines[0].text, QByteArray(kMax - 1, 'x'));
    QVERIFY(lines[0].truncated);

    lines = splitAll(QByteArray(kMax - 2, 'x') + emoji + "tail\n", 1000);
    QCOMPARE(lines[0].text, QByteArray(kMax - 2, 'x'));

    // A character that ends exactly at the limit is kept.
    lines = splitAll(QByteArray(kMax - 4, 'x') + emoji + "tail\n", 1000);
    QCOMPARE(lines[0].text, QByteArray(kMax - 4, 'x') + emoji);
    QVERIFY(!QString::fromUtf8(lines[0].text).contains(QChar::ReplacementCharacter));
}

void TestLineSplitter::skipsRestOfLongLineUntilNewline() {
    LineSplitter splitter;
    // The start of a long line is passed on as soon as it is known to be too long.
    QVector<LineSplitter::Line> lines = splitter.append(QByteArray(70'000, 'x'));
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines[0].truncated);
    QVERIFY(splitter.append(QByteArray(200'000, 'y')).isEmpty());
    QVERIFY(splitter.append("more").isEmpty());
    QCOMPARE(textsOf(splitter.append("\nnext\n")), QByteArrayList{"next"});

    // A long line at the end of the stream was already passed on.
    QCOMPARE(splitter.append(QByteArray(70'000, 'z')).size(), 1);
    QVERIFY(!splitter.finish());
    QCOMPARE(textsOf(splitter.append("fresh\n")), QByteArrayList{"fresh"});
}
