#include "tst_recordtablemodel.h"

#include "Format.h"
#include "RecordTableModel.h"
#include "Theme.h"

#include <QAbstractItemModelTester>
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace {

// 2025-11-20 14:05:01.123 UTC.
constexpr qint64 kTime = 1'763'647'501'123;

Record makeRecord(qint64 id, Level level, const QString& message, const QString& raw = {}) {
    Record record;
    record.id = id;
    record.ts = kTime + id;
    record.level = level;
    record.source = u"payment"_s;
    record.message = message;
    record.raw = raw.isEmpty() ? message : raw;
    return record;
}

QVariant cell(const RecordTableModel& model, int row, int column, int role = Qt::DisplayRole) {
    return model.data(model.index(row, column), role);
}

} // namespace

void TestRecordTableModel::startsEmpty() {
    RecordTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), int(RecordTableModel::ColumnCount));
    QVERIFY(!cell(model, 0, RecordTableModel::MessageColumn).isValid());
}

void TestRecordTableModel::showsHeaders() {
    RecordTableModel model;
    const QStringList expected = {u"Time"_s, u"Level"_s, u"Source"_s, u"Message"_s};
    for (int column = 0; column < RecordTableModel::ColumnCount; ++column) {
        QCOMPARE(model.headerData(column, Qt::Horizontal).toString(), expected[column]);
    }
}

void TestRecordTableModel::showsRecordTexts() {
    RecordTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setRecords({makeRecord(2, Level::Error, u"Payment provider timeout"_s),
                      makeRecord(1, Level::Info, u"Order created"_s)});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(cell(model, 0, RecordTableModel::TimeColumn).toString(),
             Format::timestamp(kTime + 2, QTimeZone(QTimeZone::LocalTime)));
    QCOMPARE(cell(model, 0, RecordTableModel::LevelColumn).toString(), u"ERROR"_s);
    QCOMPARE(cell(model, 0, RecordTableModel::SourceColumn).toString(), u"payment"_s);
    QCOMPARE(cell(model, 0, RecordTableModel::MessageColumn).toString(), u"Payment provider timeout"_s);
    QCOMPARE(cell(model, 1, RecordTableModel::LevelColumn).toString(), u"INFO"_s);
    QCOMPARE(model.record(1).id, 1);
}

void TestRecordTableModel::showsTimesInTheChosenZone() {
    RecordTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setRecords({makeRecord(2, Level::Info, u"a"_s)});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy header(&model, &QAbstractItemModel::headerDataChanged);
    model.setTimeZone(QTimeZone(QTimeZone::UTC));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(header.count(), 1);
    QCOMPARE(cell(model, 0, RecordTableModel::TimeColumn).toString(), u"2025-11-20 14:05:01.125"_s);
    QCOMPARE(model.headerData(RecordTableModel::TimeColumn, Qt::Horizontal).toString(), u"Time (UTC)"_s);
    model.setTimeZone(QTimeZone(QTimeZone::LocalTime));
    QCOMPARE(model.headerData(RecordTableModel::TimeColumn, Qt::Horizontal).toString(), u"Time"_s);

    // The helpers the other widgets write times with.
    QCOMPARE(Format::dateTime(kTime, QTimeZone(QTimeZone::UTC)), u"2025-11-20 14:05:01"_s);
    QCOMPARE(Format::dateTime(kTime, QTimeZone::fromSecondsAheadOfUtc(-3600)), u"2025-11-20 13:05:01"_s);
    QCOMPARE(Format::timeZoneName(QTimeZone(QTimeZone::UTC)), u"UTC"_s);
    QCOMPARE(Format::timeZoneName(QTimeZone(QTimeZone::LocalTime)), u"local time"_s);
}

void TestRecordTableModel::showsFirstLineOfMessage() {
    RecordTableModel model;
    model.setRecords({makeRecord(1, Level::Error, u"Unhandled exception\n  at Cart.add()\n  at main()"_s),
                      makeRecord(2, Level::Warn, u"Windows line\r\nsecond line"_s),
                      makeRecord(3, Level::Info, u"\nstarts with a break"_s)});
    QCOMPARE(cell(model, 0, RecordTableModel::MessageColumn).toString(), u"Unhandled exception"_s);
    QCOMPARE(cell(model, 1, RecordTableModel::MessageColumn).toString(), u"Windows line"_s);
    QCOMPARE(cell(model, 2, RecordTableModel::MessageColumn).toString(), QString());
}

void TestRecordTableModel::cutsLongLinesBetweenCharacters() {
    // A character outside the Basic Multilingual Plane (an emoji) straddles the limit.
    const QString emoji = QString(QChar::highSurrogate(0x1F600)) + QChar::lowSurrogate(0x1F600);
    const QString straddling = QString(Format::kMaxShownLength - 1, u'a') + emoji + u"tail"_s;
    RecordTableModel model;
    model.setRecords(
        {makeRecord(1, Level::Info, QString(5000, u'x')), makeRecord(2, Level::Info, straddling)});

    QCOMPARE(cell(model, 0, RecordTableModel::MessageColumn).toString().size(), Format::kMaxShownLength);
    QCOMPARE(cell(model, 1, RecordTableModel::MessageColumn).toString(),
             QString(Format::kMaxShownLength - 1, u'a'));
}

void TestRecordTableModel::colorsLevelsAndUsesFonts() {
    RecordTableModel model;
    model.setRecords(
        {makeRecord(1, Level::Fatal, u"Out of memory"_s), makeRecord(2, Level::Unknown, u"?"_s)});

    // The level colors, made readable on the background of the rows.
    const QColor base = QGuiApplication::palette().color(QPalette::Base);
    QCOMPARE(cell(model, 0, RecordTableModel::LevelColumn, Qt::ForegroundRole).value<QColor>(),
             Theme::levelTextColor(Level::Fatal, base));
    QCOMPARE(cell(model, 1, RecordTableModel::LevelColumn, Qt::ForegroundRole).value<QColor>(),
             Theme::levelTextColor(Level::Unknown, base));
    QVERIFY(!cell(model, 0, RecordTableModel::MessageColumn, Qt::ForegroundRole).isValid());

    QVERIFY(cell(model, 0, RecordTableModel::LevelColumn, Qt::FontRole).value<QFont>().bold());
    QCOMPARE(cell(model, 0, RecordTableModel::MessageColumn, Qt::FontRole).value<QFont>().family(),
             Theme::monospaceFont().family());
    QVERIFY(!cell(model, 0, RecordTableModel::TimeColumn, Qt::FontRole).isValid());
}

void TestRecordTableModel::copiesRawLinesInRowOrder() {
    RecordTableModel model;
    model.setRecords({makeRecord(1, Level::Info, u"first"_s, u"{\"msg\":\"first\"}"_s),
                      makeRecord(2, Level::Info, u"second"_s, u"second raw\n  at frame"_s),
                      makeRecord(3, Level::Info, u"third"_s, u"third raw"_s)});
    QCOMPARE(model.rawLines({2, 0}), u"{\"msg\":\"first\"}\nthird raw"_s);
    QCOMPARE(model.rawLines({1}), u"second raw\n  at frame"_s);
    QCOMPARE(model.rawLines({7, -1}), QString());
}

void TestRecordTableModel::replacesRowsWithReset() {
    RecordTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.setRecords({makeRecord(1, Level::Info, u"one"_s)});
    model.setRecords({makeRecord(2, Level::Info, u"two"_s), makeRecord(3, Level::Info, u"three"_s)});
    model.setRecords({});
    QCOMPARE(resets.count(), 3);
    QCOMPARE(model.rowCount(), 0);
}
