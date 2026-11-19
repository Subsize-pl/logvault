#pragma once

#include <QObject>

/// Checks TimeParser: every timestamp format of DESIGN.md section 5.4, zones, Unix time and garbage.
class TestTimeParser : public QObject {
    Q_OBJECT

private slots:
    void parsesTimestamps_data();
    void parsesTimestamps();
    void rejectsGarbage_data();
    void rejectsGarbage();
    void findsTimestampAtStartOfText();
    void readsJsonNumbers();
    void zonedTimesMatchQDateTime();
};
