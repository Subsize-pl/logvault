#pragma once

#include "Exporter.h"
#include "Record.h"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <memory>
#include <optional>

/// Checks Exporter on temporary databases: CSV quoting, defused formulas, the byte order mark and the
/// separator, JSON Lines that parse back to the records, all matches in order, and that a cancelled or failed
/// export leaves no file behind. Any Qt warning fails a test.
class TestExporter : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void csvQuotesFieldsThatNeedIt();
    void csvUsesTheGivenSeparator();
    void csvTimesFollowTheTimeZone();
    void csvDefusesFormulas();
    void jsonLinesParseBackToTheRecords();
    void exportsEveryMatchNewestFirst();
    void cancelLeavesNoFile();
    void reportsErrorsWithoutWritingFiles();
    void listSeparatorIsUsable();

private:
    QString databasePath() const;
    QString filePath(const QString& name) const;
    bool fill(const QVector<Record>& records) const;
    /// Runs an export to the end; nullopt if it did not start or end in time.
    std::optional<ExportSummary> exportTo(const QString& path, ExportFormat format, QChar separator = u',',
                                          const SearchRequest& request = {},
                                          const QTimeZone& zone = QTimeZone(QTimeZone::LocalTime)) const;

    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
