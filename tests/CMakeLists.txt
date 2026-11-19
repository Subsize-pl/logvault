qt_add_executable(lv_tests
    ConnectionGuard.h
    StoredRecords.h
    main.cpp
    tst_environment.cpp
    tst_environment.h
    tst_intake.cpp
    tst_intake.h
    tst_lineparser.cpp
    tst_lineparser.h
    tst_linesplitter.cpp
    tst_linesplitter.h
    tst_storage.cpp
    tst_storage.h
    tst_timeparser.cpp
    tst_timeparser.h
)

target_link_libraries(lv_tests PRIVATE lv_core Qt6::Test)
lv_enable_warnings(lv_tests)

add_test(NAME lv_tests COMMAND lv_tests)
# Tests never need a desktop session, even if one of them creates GUI objects.
set_tests_properties(lv_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
