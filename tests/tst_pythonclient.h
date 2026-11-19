#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

/// Runs examples/python_client.py against the real intake: every line it sends over TCP must be stored
/// exactly once, and its demo file must import without unparsed lines. Skipped when Python 3 is not in PATH.
class TestPythonClient : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void countLinesAllArrive();
    void demoFileImportsCleanly();

private:
    QString databasePath() const;
    bool runClient(const QStringList& arguments, QString& output) const;

    QString m_python;
    QString m_script;
    // A fresh directory per test, so every test starts without files.
    std::unique_ptr<QTemporaryDir> m_dir;
};
