#include "tst_databasejobs.h"
#include "tst_environment.h"
#include "tst_fileimporter.h"
#include "tst_intake.h"
#include "tst_lineparser.h"
#include "tst_linesplitter.h"
#include "tst_pythonclient.h"
#include "tst_recordtablemodel.h"
#include "tst_search.h"
#include "tst_searchquery.h"
#include "tst_searchservice.h"
#include "tst_storage.h"
#include "tst_timeparser.h"

#include <QApplication>
#include <QTest>

namespace {

// Each test object is destroyed right after its own tests, so anything it owns (an open database, say)
// is released before the next class starts.
template <typename TestClass> int runTests(int argc, char* argv[]) {
    TestClass tests;
    return QTest::qExec(&tests, argc, argv);
}

} // namespace

// Runs every test class; QTest command-line options apply to each of them.
int main(int argc, char* argv[]) {
    // Without a console window (ctest pipes, IDE runners) Qt on Windows sends test output and warnings to
    // the debugger only. Force stdout/stderr so the results are always visible; must precede any output.
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    // The models and widgets of lv_gui need a QApplication; ctest runs it on the offscreen platform.
    QApplication app(argc, argv);

    int failed = 0;
    failed += runTests<TestEnvironment>(argc, argv);
    failed += runTests<TestStorage>(argc, argv);
    failed += runTests<TestTimeParser>(argc, argv);
    failed += runTests<TestLineParser>(argc, argv);
    failed += runTests<TestLineSplitter>(argc, argv);
    failed += runTests<TestIntake>(argc, argv);
    failed += runTests<TestFileImporter>(argc, argv);
    failed += runTests<TestPythonClient>(argc, argv);
    failed += runTests<TestSearchQuery>(argc, argv);
    failed += runTests<TestSearch>(argc, argv);
    failed += runTests<TestSearchService>(argc, argv);
    failed += runTests<TestDatabaseJobs>(argc, argv);
    failed += runTests<TestRecordTableModel>(argc, argv);
    return failed == 0 ? 0 : 1;
}
