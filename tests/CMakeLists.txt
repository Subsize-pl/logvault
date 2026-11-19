qt_add_executable(lv_tests
    main.cpp
    tst_environment.cpp
    tst_environment.h
)

target_link_libraries(lv_tests PRIVATE lv_core Qt6::Test)
lv_enable_warnings(lv_tests)

add_test(NAME lv_tests COMMAND lv_tests)
# Tests never need a desktop session, even if one of them creates GUI objects.
set_tests_properties(lv_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
