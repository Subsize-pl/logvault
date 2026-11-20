qt_add_executable(lv_tests
    ConnectionGuard.h
    IntakeRig.h
    StoredRecords.h
    main.cpp
    tst_databasejobs.cpp
    tst_databasejobs.h
    tst_environment.cpp
    tst_environment.h
    tst_fileimporter.cpp
    tst_fileimporter.h
    tst_intake.cpp
    tst_intake.h
    tst_lineparser.cpp
    tst_lineparser.h
    tst_linesplitter.cpp
    tst_linesplitter.h
    tst_pythonclient.cpp
    tst_pythonclient.h
    tst_recordtablemodel.cpp
    tst_recordtablemodel.h
    tst_search.cpp
    tst_search.h
    tst_searchquery.cpp
    tst_searchquery.h
    tst_searchservice.cpp
    tst_searchservice.h
    tst_storage.cpp
    tst_storage.h
    tst_timeparser.cpp
    tst_timeparser.h
)

target_link_libraries(lv_tests PRIVATE lv_core lv_gui Qt6::Test)
# The Python client check runs examples/python_client.py from the source tree.
target_compile_definitions(lv_tests PRIVATE LV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
lv_enable_warnings(lv_tests)

add_test(NAME lv_tests COMMAND lv_tests)
# Tests never need a desktop session, even if one of them creates GUI objects.
set_tests_properties(lv_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
