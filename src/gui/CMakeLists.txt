# Everything but main.cpp is a library, so the tests can check the models and widgets too.
qt_add_library(lv_gui STATIC
    Format.cpp
    Format.h
    LiveUpdater.cpp
    LiveUpdater.h
    MainWindow.cpp
    MainWindow.h
    RecordTableModel.cpp
    RecordTableModel.h
    Theme.cpp
    Theme.h
)

target_include_directories(lv_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lv_gui PUBLIC lv_core Qt6::Widgets)
lv_enable_warnings(lv_gui)

qt_add_executable(LogVault
    main.cpp
)

# A GUI application: no console window. Qt's entry point library provides WinMain and calls main().
set_target_properties(LogVault PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(LogVault PRIVATE lv_gui)
lv_enable_warnings(LogVault)
